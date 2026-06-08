/*
 * Bluetooth A2DP Source audio driver for Retro-Go //fix 2026-06-08 new file
 * Streams emulator audio to Bluetooth headphones/speakers via Classic BT
 * Uses ESP-IDF Bluedroid A2DP Source API with SBC encoding
 *
 * Designed to coexist with BLE HID gamepad (rg_input_bluetooth.c)
 * on ESP32-S31 dual-mode Bluetooth.
 */

#include "rg_audio.h"
#include "rg_system.h"

#if RG_AUDIO_USE_BT_A2DP

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "drivers/input/rg_input_bluetooth.h"

static const char *TAG = "bt_a2dp";

/* ── Configuration ────────────────────────────────────────────────── */

#define BT_A2DP_RING_CHUNKS      32
#define BT_A2DP_CHUNK_FRAMES     256
#define BT_A2DP_TASK_STACK       4096
#define BT_A2DP_TASK_PRIO        5
#define BT_A2DP_SAMPLE_RATE      44100
#define BT_A2DP_CHANNELS         2
#define BT_A2DP_BITS_PER_SAMPLE  16
#define BT_A2DP_SCAN_DURATION    6

/* ── A2DP application states ──────────────────────────────────────── */

enum {
    A2DP_STATE_IDLE,
    A2DP_STATE_DISCOVERING,
    A2DP_STATE_DISCOVERED,
    A2DP_STATE_CONNECTING,
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STREAMING,
    A2DP_STATE_DISCONNECTING,
};

/* ── Work dispatch (simplified bt_app_core) ───────────────────────── */

typedef void (*bt_app_cb_t)(uint16_t event, void *param);

typedef struct {
    uint16_t sig;
    uint16_t event;
    bt_app_cb_t cb;
    void *param;
} bt_app_msg_t;

enum { BT_APP_SIG_WORK_DISPATCH = 0 };

static QueueHandle_t s_bt_app_queue = NULL;
static TaskHandle_t s_bt_app_task = NULL;

static bool bt_app_send_msg(bt_app_msg_t *msg)
{
    if (!msg || !s_bt_app_queue)
        return false;
    return xQueueSend(s_bt_app_queue, msg, pdMS_TO_TICKS(10)) == pdTRUE;
}

static bool bt_app_work_dispatch(bt_app_cb_t cb, uint16_t event, void *p, int len)
{
    bt_app_msg_t msg = {
        .sig = BT_APP_SIG_WORK_DISPATCH,
        .event = event,
        .cb = cb,
    };
    if (len > 0 && p) {
        msg.param = malloc(len);
        if (!msg.param)
            return false;
        memcpy(msg.param, p, len);
    }
    return bt_app_send_msg(&msg);
}

static void bt_app_task_handler(void *arg)
{
    bt_app_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_bt_app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.cb)
                msg.cb(msg.event, msg.param);
            free(msg.param);
        }
    }
}

static void bt_app_task_start(void)
{
    if (s_bt_app_task)
        return;
    s_bt_app_queue = xQueueCreate(16, sizeof(bt_app_msg_t));
    xTaskCreate(bt_app_task_handler, "a2dp_wq", 3072, NULL, 10, &s_bt_app_task);
}

static void bt_app_task_stop(void)
{
    if (s_bt_app_task) {
        vTaskDelete(s_bt_app_task);
        s_bt_app_task = NULL;
    }
    if (s_bt_app_queue) {
        bt_app_msg_t msg;
        while (xQueueReceive(s_bt_app_queue, &msg, 0) == pdTRUE)
            free(msg.param);
        vQueueDelete(s_bt_app_queue);
        s_bt_app_queue = NULL;
    }
}

/* ── Internal state ───────────────────────────────────────────────── */

static struct {
    const char *last_error;
    int a2dp_state;
    int sample_rate;
    int volume;
    bool muted;
    bool stack_initialized;       /* Did we init BT stack ourselves? */
    bool a2dp_initialized;
    bool avrc_initialized;
    esp_bd_addr_t peer_bda;
    char peer_name[64];
    bool peer_found;
    RingbufHandle_t ringbuf;
    SemaphoreHandle_t mutex;
    uint32_t submit_count;
    uint32_t data_cb_count;
} state;

/* ── Helpers ──────────────────────────────────────────────────────── */

static bool set_error(const char *error)
{
    state.last_error = error;
    ESP_LOGE(TAG, "%s", error);
    return false;
}

static const char *conn_state_str[] = {
    "Disconnected", "Connecting", "Connected", "Disconnecting"
};

/* ── GAP callback (Classic BT discovery) ──────────────────────────── */

static bool has_rendering_class(uint32_t cod)
{
    /* Check MAJOR SERVICE CLASS: Rendering (bit 18) */
    return (cod & 0x00040000) != 0;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *name, uint8_t *name_len)
{
    if (!eir)
        return false;
    uint8_t *rmt = NULL, rmt_len = 0;
    rmt = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_len);
    if (!rmt)
        rmt = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_len);
    if (rmt) {
        if (rmt_len > ESP_BT_GAP_MAX_BDNAME_LEN)
            rmt_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        if (name) {
            memcpy(name, rmt, rmt_len);
            name[rmt_len] = '\0';
        }
        if (name_len)
            *name_len = rmt_len;
        return true;
    }
    return false;
}

static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        if (state.a2dp_state != A2DP_STATE_DISCOVERING)
            break;
        uint32_t cod = 0;
        uint8_t *eir = NULL;
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_COD)
                cod = *(uint32_t *)param->disc_res.prop[i].val;
            else if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR)
                eir = (uint8_t *)param->disc_res.prop[i].val;
        }
        if (!has_rendering_class(cod))
            break;
        uint8_t name[ESP_BT_GAP_MAX_BDNAME_LEN + 1] = {0};
        if (get_name_from_eir(eir, name, NULL)) {
            ESP_LOGI(TAG, "Found audio sink: %s", name);
            memcpy(state.peer_bda, param->disc_res.bda, sizeof(esp_bd_addr_t));
            strncpy(state.peer_name, (char *)name, sizeof(state.peer_name) - 1);
            state.peer_found = true;
            state.a2dp_state = A2DP_STATE_DISCOVERED;
            esp_bt_gap_cancel_discovery();
        }
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            if (state.a2dp_state == A2DP_STATE_DISCOVERED && state.peer_found) {
                ESP_LOGI(TAG, "Connecting A2DP to %s", state.peer_name);
                state.a2dp_state = A2DP_STATE_CONNECTING;
                esp_a2d_source_connect(state.peer_bda);
            } else if (state.a2dp_state == A2DP_STATE_DISCOVERING) {
                ESP_LOGI(TAG, "No audio sink found, retrying...");
                esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_A2DP_SCAN_DURATION, 0);
            }
        }
        break;
    }
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "Auth: %s", param->auth_cmpl.stat == 0 ? "OK" : "FAIL");
        break;
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = {0};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 0, pin);
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    default:
        break;
    }
}

/* ── A2DP Source callback ─────────────────────────────────────────── */

static void a2dp_event_handler(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        ESP_LOGI(TAG, "A2DP conn: %s", conn_state_str[param->conn_stat.state]);
        if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            state.a2dp_state = A2DP_STATE_CONNECTED;
            /* Make non-discoverable once connected */
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            state.a2dp_state = A2DP_STATE_IDLE;
            /* Make discoverable again */
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            /* Restart discovery */
            state.peer_found = false;
            state.a2dp_state = A2DP_STATE_DISCOVERING;
            esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_A2DP_SCAN_DURATION, 0);
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        ESP_LOGI(TAG, "A2DP audio: %s", param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED ? "started" : "suspended");
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            state.a2dp_state = A2DP_STATE_STREAMING;
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "A2DP codec configured: type=%d", param->audio_cfg.mcc.type);
        break;
    case ESP_A2D_PROF_STATE_EVT:
        ESP_LOGI(TAG, "A2DP profile: %s", param->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS ? "init OK" : "deinit OK");
        break;
    case ESP_A2D_REPORT_SNK_CODEC_CAPS_EVT: {
        esp_a2d_mcc_t *mcc = &param->a2d_report_snk_codec_caps_stat.mcc;
        ESP_LOGI(TAG, "Sink codec caps: type=%d", mcc->type);
        /* Set preferred SBC config: 44.1kHz Joint Stereo */
        esp_a2d_mcc_t pref = {
            .type = ESP_A2D_MCT_SBC,
            .cie.sbc_info = {
                .samp_freq = ESP_A2D_SBC_CIE_SF_44K,
                .ch_mode = ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO,
                .block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_16,
                .num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_8,
                .alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS,
                .min_bitpool = 2,
                .max_bitpool = 53,
            },
        };
        esp_a2d_source_set_pref_mcc(param->a2d_report_snk_codec_caps_stat.conn_hdl, &pref);
        break;
    }
    default:
        break;
    }
}

/* ── A2DP Source data callback (pulls PCM from ring buffer) ───────── */

static int32_t a2dp_data_callback(uint8_t *data, int32_t len)
{
    if (!data || len <= 0 || !state.ringbuf)
        return 0;

    size_t needed = (size_t)len;
    size_t filled = 0;

    while (filled < needed) {
        size_t bytes = 0;
        void *chunk = xRingbufferReceiveUpTo(state.ringbuf, &bytes, pdMS_TO_TICKS(20),
                                              needed - filled);
        if (!chunk || bytes == 0) {
            /* Underrun: fill silence */
            memset(data + filled, 0, needed - filled);
            filled = needed;
            break;
        }
        memcpy(data + filled, chunk, bytes);
        vRingbufferReturnItem(state.ringbuf, chunk);
        filled += bytes;
    }

    state.data_cb_count++;
    return (int32_t)filled;
}

/* ── AVRC Controller callback ─────────────────────────────────────── */

static void avrc_event_handler(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "AVRC conn: %s", param->conn_stat.connected ? "connected" : "disconnected");
        if (param->conn_stat.connected)
            esp_avrc_ct_send_get_rn_capabilities_cmd(0);
        break;
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        esp_avrc_rn_evt_cap_mask_t cap = param->get_rn_caps_rsp.evt_set;
        if (cap.bits & (1 << ESP_AVRC_RN_VOLUME_CHANGE)) {
            esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_VOLUME_CHANGE, 0);
        }
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        if (param->change_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
            int vol = param->change_ntf.event_parameter.volume;
            ESP_LOGI(TAG, "AVRC volume: %d", vol);
            /* Re-register for next notification */
            esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_VOLUME_CHANGE, 0);
        }
        break;
    }
    default:
        break;
    }
}

/* ── Driver interface ─────────────────────────────────────────────── */

static bool driver_init(int device, int sample_rate)
{
    (void)device;
    memset(&state, 0, sizeof(state));
    state.sample_rate = sample_rate > 0 ? sample_rate : BT_A2DP_SAMPLE_RATE;
    state.volume = rg_audio_get_volume();

    state.mutex = xSemaphoreCreateMutex();
    if (!state.mutex)
        return set_error("Failed to create mutex");

    /* Create ring buffer */
    state.ringbuf = xRingbufferCreate(
        BT_A2DP_CHUNK_FRAMES * sizeof(rg_audio_frame_t) * BT_A2DP_RING_CHUNKS,
        RINGBUF_TYPE_NOSPLIT);
    if (!state.ringbuf)
        return set_error("Failed to create ring buffer");

    /*
     * Acquire shared BT stack (ref-counted).
     * The HID bluetooth driver may have already initialized the BT controller
     * and Bluedroid. This is safe — we just bump the refcount.
     */
    if (!rg_bt_stack_acquire(RG_BT_TRANSPORT_DUAL)) {
        return set_error("Failed to acquire BT stack");
    }
    state.stack_initialized = true;

    /* Start work dispatch task */
    bt_app_task_start();

    /* Set Classic BT device name */
    esp_bt_gap_set_device_name("Retro-Go S31");

    /* Register GAP callback for Classic BT */
    esp_bt_gap_register_callback(gap_event_handler);

    /* Init A2DP Source */
    esp_err_t ret = esp_a2d_source_init();
    if (ret) {
        ESP_LOGE(TAG, "A2DP source init failed: %s", esp_err_to_name(ret));
        return set_error("A2DP source init failed");
    }
    state.a2dp_initialized = true;
    esp_a2d_register_callback(a2dp_event_handler);
    esp_a2d_source_register_data_callback(a2dp_data_callback);

    /* Init AVRC Controller */
    ret = esp_avrc_ct_init();
    if (ret) {
        ESP_LOGE(TAG, "AVRC CT init failed: %s", esp_err_to_name(ret));
        /* Non-fatal: continue without AVRC */
    } else {
        state.avrc_initialized = true;
        esp_avrc_ct_register_callback(avrc_event_handler);
    }

    /* SSP */
    esp_bt_sp_param_t sp_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(sp_type, &iocap, sizeof(uint8_t));

    /* Set discoverable and connectable */
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    /* Start scanning for audio sinks */
    state.a2dp_state = A2DP_STATE_DISCOVERING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, BT_A2DP_SCAN_DURATION, 0);

    ESP_LOGI(TAG, "A2DP Source initialized, scanning for audio devices...");
    return true;
}

static bool driver_deinit(void)
{
    /* Stop A2DP source */
    if (state.a2dp_initialized) {
        esp_a2d_source_deinit();
        state.a2dp_initialized = false;
    }

    /* Stop AVRC */
    if (state.avrc_initialized) {
        esp_avrc_ct_deinit();
        state.avrc_initialized = false;
    }

    /* Stop work dispatch */
    bt_app_task_stop();

    /* Release shared BT stack (ref-counted) */
    if (state.stack_initialized) {
        rg_bt_stack_release();
        state.stack_initialized = false;
    }

    /* Clean up ring buffer */
    if (state.ringbuf) {
        vRingbufferDelete(state.ringbuf);
        state.ringbuf = NULL;
    }

    /* Release mutex */
    if (state.mutex) {
        vSemaphoreDelete(state.mutex);
        state.mutex = NULL;
    }

    ESP_LOGI(TAG, "A2DP Source deinitialized");
    memset(&state, 0, sizeof(state));
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    if (!state.ringbuf || !state.a2dp_initialized)
        return false;

    if (state.muted) {
        /* Submit silence when muted */
        static const rg_audio_frame_t silence[256] = {0};
        while (count > 0) {
            size_t n = count > 256 ? 256 : count;
            xRingbufferSend(state.ringbuf, (void *)silence, n * sizeof(rg_audio_frame_t), pdMS_TO_TICKS(100));
            count -= n;
        }
        return true;
    }

    size_t bytes = count * sizeof(rg_audio_frame_t);
    if (xRingbufferSend(state.ringbuf, (void *)frames, bytes, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Ring buffer full, drop %u frames", (unsigned)count);
    }

    state.submit_count++;
    return true;
}

static bool driver_set_mute(bool mute)
{
    state.muted = mute;
    ESP_LOGI(TAG, "Mute: %d", mute);
    return true;
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    /* AVRC absolute volume would be set here if connected */
    ESP_LOGI(TAG, "Volume: %d%%", volume);
    return true;
}

static bool driver_set_sample_rate(int sample_rate)
{
    if (sample_rate == state.sample_rate)
        return true;
    state.sample_rate = sample_rate;
    ESP_LOGI(TAG, "Sample rate: %d", sample_rate);
    return true;
}

static const char *driver_get_error(void)
{
    return state.last_error;
}

const rg_audio_driver_t rg_audio_driver_bt_a2dp = {
    .name = "bt_a2dp",
    .init = driver_init,
    .deinit = driver_deinit,
    .submit = driver_submit,
    .set_mute = driver_set_mute,
    .set_volume = driver_set_volume,
    .set_sample_rate = driver_set_sample_rate,
    .get_error = driver_get_error,
};

#endif /* RG_AUDIO_USE_BT_A2DP */
