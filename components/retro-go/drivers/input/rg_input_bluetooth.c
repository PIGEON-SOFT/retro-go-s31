/*
 * Classic Bluetooth Xbox gamepad driver for Retro-Go (ESP32-S31) //fix 2026-06-08 new file
 * Uses Bluedroid Classic BT HID Host to connect to Xbox Wireless Controller.
 */

#include "rg_system.h"
#include "rg_input.h"
#include "rg_input_bluetooth.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_heap_caps.h"

static const char *TAG = "rg_bt";

static void log_mem(const char *tag)
{
    ESP_LOGI(TAG, "[%s] free: int=%u dma=%u psram=%u",
             tag,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* ── State ────────────────────────────────────────────────────────── */

typedef enum {
    BT_STATE_OFF = 0,
    BT_STATE_IDLE,
    BT_STATE_SCANNING,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
} bt_state_t;

typedef struct scan_node {
    rg_bt_device_info_t info;
    struct scan_node *next;
} scan_node_t;

static struct {
    bt_state_t state;
    bool initialized;
    bool connected;
    uint32_t gamepad_state;
    SemaphoreHandle_t mutex;
    esp_hidh_dev_t *hid_dev;
    rg_bt_device_info_t connected_device;
    rg_bt_device_info_t saved_device;
    bool has_saved_device;
    scan_node_t *scan_list;
    size_t scan_count;
} bt;

#define NVS_NS "bluetooth"
#define NVS_KEY_BDA "bda"
#define NVS_KEY_NAME "name"

/* ── Input parsing ────────────────────────────────────────────────── */

static uint32_t parse_xbox_bt(const uint8_t *d, size_t len)
{
    /* Xbox Wireless Controller Classic BT HID report (verified via raw dump):
     * Bytes  0-1 : Left stick X  (uint16 LE, center=0x8000)
     * Bytes  2-3 : Left stick Y  (uint16 LE, center=0x8000)
     * Bytes  4-5 : Right stick X (uint16 LE, center=0x8000)
     * Bytes  6-7 : Right stick Y (uint16 LE, center=0x8000)
     * Bytes  8-9 : Left trigger  (uint16 LE, 0-0x3FF)
     * Bytes 10-11: Right trigger (uint16 LE, 0-0x3FF)
     * Byte  12  : D-pad (0=up,1=up-right,2=right,...,7=up-left, 0x00=idle)
     * Byte  13  : Buttons A/B/X/Y/LB/RB
     *   bit0=B, bit1=A, bit3=Y, bit4=X, bit6=LB, bit7=RB
     * Byte  14  : Center buttons
     *   bit2=Select(-), bit3=Start(+)
     * Byte  15  : Special buttons
     *   bit0=Xbox/Home
     */
    if (len < 16) return 0;

    uint8_t dpad = d[12];
    uint8_t btn = d[13];
    uint8_t btn2 = d[14];
    uint8_t btn3 = d[15];
    uint32_t s = 0;

    /* D-pad: 0x00=idle, 1=up, 2=up-right, 3=right, 4=down-right,
     *        5=down, 6=down-left, 7=left, 8=up-left */
    switch (dpad) {
        case 1: s |= RG_KEY_UP; break;
        case 2: s |= RG_KEY_UP | RG_KEY_RIGHT; break;
        case 3: s |= RG_KEY_RIGHT; break;
        case 4: s |= RG_KEY_DOWN | RG_KEY_RIGHT; break;
        case 5: s |= RG_KEY_DOWN; break;
        case 6: s |= RG_KEY_DOWN | RG_KEY_LEFT; break;
        case 7: s |= RG_KEY_LEFT; break;
        case 8: s |= RG_KEY_UP | RG_KEY_LEFT; break;
    }

    /* Main buttons (byte 13) */
    if (btn & 0x02) s |= RG_KEY_A;       /* A */
    if (btn & 0x01) s |= RG_KEY_B;       /* B */
    if (btn & 0x10) s |= RG_KEY_X;       /* X */
    if (btn & 0x08) s |= RG_KEY_Y;       /* Y */
    if (btn & 0x40) s |= RG_KEY_L;       /* LB */
    if (btn & 0x80) s |= RG_KEY_R;       /* RB */

    /* Center buttons (byte 14) */
    if (btn2 & 0x04) s |= RG_KEY_SELECT;  /* Select/Back (-) */
    if (btn2 & 0x08) s |= RG_KEY_START;   /* Start (+) */

    /* Special buttons (byte 15) */
    if (btn3 & 0x01) s |= RG_KEY_MENU;    /* Xbox/Home */

    return s;
}

static uint32_t parse_hid(const uint8_t *d, size_t len, esp_hid_usage_t usage)
{
    if (len < 2) return 0;

    /* Consumer control (volume, media keys) */
    if (usage == ESP_HID_USAGE_CCONTROL) {
        uint16_t cc = (d[1] << 8) | d[0];
        uint32_t s = 0;
        if (cc == 0x0040) s |= RG_KEY_UP;
        if (cc == 0x0080) s |= RG_KEY_DOWN;
        if (cc == 0x0001) s |= RG_KEY_LEFT;
        if (cc == 0x0002) s |= RG_KEY_RIGHT;
        return s;
    }

    /* Xbox controller — 14 byte report */
    if (len >= 13) return parse_xbox_bt(d, len);

    /* Generic gamepad fallback (D-pad byte + buttons byte) */
    uint32_t s = 0;
    switch (d[0] & 0x0F) {
        case 0: s |= RG_KEY_UP; break;
        case 1: s |= RG_KEY_UP | RG_KEY_RIGHT; break;
        case 2: s |= RG_KEY_RIGHT; break;
        case 3: s |= RG_KEY_DOWN | RG_KEY_RIGHT; break;
        case 4: s |= RG_KEY_DOWN; break;
        case 5: s |= RG_KEY_DOWN | RG_KEY_LEFT; break;
        case 6: s |= RG_KEY_LEFT; break;
        case 7: s |= RG_KEY_UP | RG_KEY_LEFT; break;
    }
    if (len > 1) {
        uint8_t b = d[1];
        if (b & 0x01) s |= RG_KEY_A;
        if (b & 0x02) s |= RG_KEY_B;
        if (b & 0x04) s |= RG_KEY_X;
        if (b & 0x08) s |= RG_KEY_Y;
        if (b & 0x10) s |= RG_KEY_L;
        if (b & 0x20) s |= RG_KEY_R;
        if (b & 0x40) s |= RG_KEY_SELECT;
        if (b & 0x80) s |= RG_KEY_START;
    }
    return s;
}

/* ── Scan list ────────────────────────────────────────────────────── */

static void scan_add(const uint8_t *bda, const char *name, int rssi)
{
    for (scan_node_t *n = bt.scan_list; n; n = n->next)
        if (memcmp(n->info.bda, bda, 6) == 0) return;

    scan_node_t *n = malloc(sizeof(scan_node_t));
    if (!n) return;
    memcpy(n->info.bda, bda, 6);
    if (name && name[0])
        strncpy(n->info.name, name, sizeof(n->info.name) - 1);
    else
        snprintf(n->info.name, sizeof(n->info.name), "%02x:%02x:%02x:%02x:%02x:%02x",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    n->info.transport = RG_BT_TRANSPORT_CLASSIC;
    n->info.rssi = rssi;
    n->info.paired = false;
    n->next = bt.scan_list;
    bt.scan_list = n;
    bt.scan_count++;
    ESP_LOGI(TAG, "Found: %s (rssi=%d)", n->info.name, rssi);
}

static void scan_free(void)
{
    scan_node_t *n = bt.scan_list;
    while (n) { scan_node_t *next = n->next; free(n); n = next; }
    bt.scan_list = NULL;
    bt.scan_count = 0;
}

/* ── Classic BT GAP callback ──────────────────────────────────────── */

static void get_name_from_eir(uint8_t *eir, char *name, size_t max_len)
{
    if (!eir || !name || max_len == 0) return;
    uint8_t nlen = 0;
    uint8_t *ndata = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &nlen);
    if (!ndata)
        ndata = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &nlen);
    if (ndata && nlen) {
        size_t n = nlen < (max_len - 1) ? nlen : (max_len - 1);
        memcpy(name, ndata, n);
        name[n] = '\0';
    }
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        char name[64] = {0};
        int8_t rssi = 0;
        uint8_t *eir = NULL;

        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
            switch (p->type) {
            case ESP_BT_GAP_DEV_PROP_RSSI:
                rssi = *(int8_t *)(p->val);
                break;
            case ESP_BT_GAP_DEV_PROP_BDNAME: {
                uint8_t len = p->len > 63 ? 63 : p->len;
                memcpy(name, p->val, len);
                name[len] = '\0';
                break;
            }
            case ESP_BT_GAP_DEV_PROP_EIR:
                eir = (uint8_t *)(p->val);
                break;
            default:
                break;
            }
        }

        if (!name[0] && eir)
            get_name_from_eir(eir, name, sizeof(name));

        if (name[0])
            scan_add(param->disc_res.bda, name, rssi);

        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Discovery done, %u devices", (unsigned)bt.scan_count);
            bt.state = BT_STATE_IDLE;
        }
        break;
    default:
        break;
    }
}

/* ── HID callback ─────────────────────────────────────────────────── */

/* Raw HID report buffer — callback copies here, parse task reads */
static uint8_t hid_raw_report[32];
static volatile bool hid_report_ready = false;

static void hidh_cb(void *a, esp_event_base_t b, int32_t id, void *d)
{
    esp_hidh_event_t ev = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *p = (esp_hidh_event_data_t *)d;

    switch (ev) {
    case ESP_HIDH_OPEN_EVENT:
        if (p->open.status == ESP_OK) {
            const uint8_t *bda = esp_hidh_dev_bda_get(p->open.dev);
            ESP_LOGI(TAG, "HID connected: %02x:%02x:%02x:%02x:%02x:%02x",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            bt.hid_dev = p->open.dev;
            bt.connected = true;
            bt.state = BT_STATE_CONNECTED;
            memcpy(bt.connected_device.bda, bda, 6);
            const char *name = esp_hidh_dev_name_get(p->open.dev);
            if (name) strncpy(bt.connected_device.name, name, sizeof(bt.connected_device.name) - 1);
            bt.connected_device.transport = RG_BT_TRANSPORT_CLASSIC;
            rg_input_bluetooth_save_device(&bt.connected_device);
            log_mem("hid_open");
        } else {
            ESP_LOGE(TAG, "HID connect fail: %d", p->open.status);
            bt.state = BT_STATE_IDLE;
        }
        break;
    case ESP_HIDH_INPUT_EVENT: {
        /* Minimal work in BTU_TASK: just copy raw bytes to buffer.
         * Parsing is done in hid_parse_task at controlled rate. */
        size_t len = p->input.length;
        if (len > sizeof(hid_raw_report)) len = sizeof(hid_raw_report);
        if (len > 0) {
            memcpy(hid_raw_report, p->input.data, len);
            hid_report_ready = true;
        }
        break;
    }
    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGI(TAG, "HID disconnected");
        bt.connected = false;
        bt.hid_dev = NULL;
        bt.state = BT_STATE_IDLE;
        bt.gamepad_state = 0;
        hid_report_ready = false;
        break;
    default:
        break;
    }
}

/* Low-priority task: parses HID reports at ~30Hz */
static void hid_parse_task(void *arg)
{
    while (bt.initialized) {
        if (bt.connected && hid_report_ready) {
            hid_report_ready = false;
            uint32_t s = parse_xbox_bt(hid_raw_report, sizeof(hid_raw_report));
            bt.gamepad_state = s;
        }
        vTaskDelay(pdMS_TO_TICKS(33)); /* ~30Hz */
    }
    vTaskDelete(NULL);
}

/* ── Reconnect task ───────────────────────────────────────────────── */

static void reconnect_task(void *arg)
{
    while (bt.initialized) {
        /* Only reconnect if: not connected, has real saved device, and idle (not scanning) */
        if (!bt.connected && bt.has_saved_device && bt.state == BT_STATE_IDLE) {
            ESP_LOGI(TAG, "Reconnecting to %s...", bt.saved_device.name);
            rg_input_bluetooth_connect(bt.saved_device.bda, RG_BT_TRANSPORT_CLASSIC);
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    vTaskDelete(NULL);
}

/* ── NVS ──────────────────────────────────────────────────────────── */

bool rg_input_bluetooth_save_device(const rg_bt_device_info_t *dev)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_blob(h, NVS_KEY_BDA, dev->bda, 6);
    nvs_set_str(h, NVS_KEY_NAME, dev->name);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Saved: %s", dev->name);
    return true;
}

bool rg_input_bluetooth_load_device(rg_bt_device_info_t *dev)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        size_t blen = 6;
        if (nvs_get_blob(h, NVS_KEY_BDA, dev->bda, &blen) == ESP_OK && blen == 6) {
            size_t nlen = sizeof(dev->name);
            nvs_get_str(h, NVS_KEY_NAME, dev->name, &nlen);
            dev->transport = RG_BT_TRANSPORT_CLASSIC;
            dev->paired = true;
            nvs_close(h);
            ESP_LOGI(TAG, "Loaded saved device: %s", dev->name);
            return true;
        }
        nvs_close(h);
    }
    /* No saved device in NVS — do NOT use default, let user scan and select */
    memset(dev, 0, sizeof(rg_bt_device_info_t));
    ESP_LOGI(TAG, "No saved device in NVS");
    return false;
    dev->transport = RG_BT_TRANSPORT_CLASSIC;
    dev->paired = true;
    ESP_LOGI(TAG, "Using default device: %s (%02x:%02x:%02x:%02x:%02x:%02x)",
             dev->name, dev->bda[0], dev->bda[1], dev->bda[2], dev->bda[3], dev->bda[4], dev->bda[5]);
    return true;
}

void rg_input_bluetooth_clear_saved_device(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    bt.has_saved_device = false;
    memset(&bt.saved_device, 0, sizeof(bt.saved_device));
}

/* ── Background init ──────────────────────────────────────────────── */

static void bt_init_task(void *arg)
{
    log_mem("init_start");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    /* Init BT controller — Classic BT mode (same as bt_discovery example) */
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&cfg);
    if (ret) { ESP_LOGE(TAG, "ctrl init fail: %d", ret); goto fail; }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret) { ESP_LOGE(TAG, "ctrl enable fail: %d", ret); goto fail; }
    log_mem("after_ctrl");

    /* Init Bluedroid */
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret) { ESP_LOGE(TAG, "bluedroid init fail: %d", ret); goto fail; }
    ret = esp_bluedroid_enable();
    if (ret) { ESP_LOGE(TAG, "bluedroid enable fail: %d", ret); goto fail; }
    log_mem("after_bluedroid");

    /* Register Classic BT GAP callback */
    esp_bt_gap_register_callback(bt_gap_cb);
    ESP_LOGI(TAG, "GAP callback registered");

    /* Init HID host */
    ret = esp_hidh_init(&(esp_hidh_config_t){ .callback = hidh_cb, .event_stack_size = 4096 });
    if (ret) { ESP_LOGE(TAG, "hidh init fail: %d", ret); goto fail; }
    log_mem("after_hidh");

    /* Mark ready */
    bt.initialized = true;
    bt.state = BT_STATE_IDLE;

    /* Start low-priority HID parse task (reads buffer at ~30Hz) */
    xTaskCreate(hid_parse_task, "bt_parse", 2048, NULL, 3, NULL);

    /* Auto-connect saved device */
    if (rg_input_bluetooth_load_device(&bt.saved_device)) {
        bt.has_saved_device = true;
        ESP_LOGI(TAG, "Saved device: %s", bt.saved_device.name);
        xTaskCreate(reconnect_task, "bt_reconn", 4096, NULL, 5, NULL);
    }

    log_mem("init_done");
    ESP_LOGI(TAG, "*** BT READY ***");
    vTaskDelete(NULL);
    return;

fail:
    log_mem("init_failed");
    ESP_LOGE(TAG, "*** BT INIT FAILED ***");
    vTaskDelete(NULL);
}

/* ── Public API ───────────────────────────────────────────────────── */

void rg_input_bluetooth_init(rg_bt_transport_t transport)
{
    if (bt.initialized || bt.state != BT_STATE_OFF) return;
    bt.mutex = xSemaphoreCreateMutex();
    bt.state = BT_STATE_IDLE;
    xTaskCreate(bt_init_task, "bt_init", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "BT init task started");
}

void rg_input_bluetooth_deinit(void)
{
    if (!bt.initialized) return;
    bt.initialized = false;
    rg_input_bluetooth_disconnect();
    esp_hidh_deinit();
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    if (bt.mutex) { vSemaphoreDelete(bt.mutex); bt.mutex = NULL; }
    scan_free();
    bt.state = BT_STATE_OFF;
}

uint32_t rg_input_bluetooth_read(void)
{
    if (!bt.initialized || !bt.connected) return 0;
    /* gamepad_state is a single uint32_t, atomic read on RISC-V */
    return bt.gamepad_state;
}

bool rg_input_bluetooth_is_connected(void) { return bt.connected; }

const char *rg_input_bluetooth_get_state_str(void)
{
    switch (bt.state) {
        case BT_STATE_OFF: return "Off";
        case BT_STATE_IDLE: return "Idle";
        case BT_STATE_SCANNING: return "Scanning";
        case BT_STATE_CONNECTING: return "Connecting";
        case BT_STATE_CONNECTED: return "Connected";
        default: return "Unknown";
    }
}

int rg_input_bluetooth_scan(int duration_sec, rg_bt_scan_callback_t callback, void *user_data)
{
    /* Wait for init */
    int waited = 0;
    while (!bt.initialized && waited < 15000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        waited += 500;
    }
    if (!bt.initialized) {
        ESP_LOGE(TAG, "BT not ready");
        return 0;
    }

    ESP_LOGI(TAG, "Scanning %d sec...", duration_sec);
    log_mem("scan_start");

    bt.state = BT_STATE_SCANNING;
    scan_free();

    /* Classic BT discovery */
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                                (int)(duration_sec / 1.28), 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "start_discovery fail: %d", ret);
        bt.state = BT_STATE_IDLE;
        return 0;
    }

    vTaskDelay(pdMS_TO_TICKS(duration_sec * 1000 + 2000));

    if (bt.state == BT_STATE_SCANNING) {
        bt.state = BT_STATE_IDLE;
    }

    /* Copy results to callback */
    if (callback) {
        for (scan_node_t *n = bt.scan_list; n; n = n->next)
            callback(&n->info, user_data);
    }

    bt.state = BT_STATE_IDLE;
    ESP_LOGI(TAG, "Scan done: %u devices", (unsigned)bt.scan_count);
    log_mem("scan_done");
    return bt.scan_count;
}

bool rg_input_bluetooth_connect(const uint8_t *bda, rg_bt_transport_t transport)
{
    if (!bt.initialized) return false;
    if (bt.connected) rg_input_bluetooth_disconnect();
    ESP_LOGI(TAG, "Connecting to %02x:%02x:%02x:%02x:%02x:%02x", bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    bt.state = BT_STATE_CONNECTING;
    esp_hidh_dev_t *dev = esp_hidh_dev_open((uint8_t *)bda, ESP_HID_TRANSPORT_BT, 0);
    if (!dev) { ESP_LOGE(TAG, "HID open fail"); bt.state = BT_STATE_IDLE; return false; }
    return true;
}

void rg_input_bluetooth_disconnect(void)
{
    if (bt.hid_dev) { esp_hidh_dev_close(bt.hid_dev); bt.hid_dev = NULL; }
    bt.connected = false;
    bt.gamepad_state = 0;
    bt.state = BT_STATE_IDLE;
}

bool rg_input_bluetooth_get_connected_device(rg_bt_device_info_t *info)
{
    if (!bt.connected) return false;
    if (info) memcpy(info, &bt.connected_device, sizeof(rg_bt_device_info_t));
    return true;
}
