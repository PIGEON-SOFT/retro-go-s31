#include "rg_audio.h"
#include "rg_i2c.h"
#include "rg_system.h"

#if RG_AUDIO_USE_ES8389

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8389_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include <string.h>

#define ES8389_SAMPLE_BITS 16
#define ES8389_CHANNELS    2
#define ES8389_I2C_PORT    I2C_NUM_0
#define ES8389_I2S_PORT    I2S_NUM_0
#define ES8389_CODEC_RATE   48000
#define ES8389_CHUNK_FRAMES 256
#define ES8389_RING_CHUNKS  24
#define ES8389_TASK_STACK   4096
#define ES8389_TASK_PRIO    6
#define ES8389_GAIN_SHIFT   2

static struct
{
    const char *last_error;
    esp_codec_dev_handle_t codec;
    const audio_codec_if_t *codec_if;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    i2s_chan_handle_t tx_chan;
    int sample_rate;
    int volume;
    uint32_t resample_accum;
    uint32_t submit_count;
    uint32_t write_count;
    RingbufHandle_t ringbuf;
    TaskHandle_t task;
    bool muted;
    bool task_running;
    bool opened;
} state;

static bool set_error(const char *error)
{
    state.last_error = error;
    return false;
}

static inline int16_t amplify_sample(int16_t sample)
{
    int out = sample << ES8389_GAIN_SHIFT;
    if (out > INT16_MAX)
        return INT16_MAX;
    if (out < INT16_MIN)
        return INT16_MIN;
    return out;
}

static bool open_codec(int sample_rate)
{
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = ES8389_SAMPLE_BITS,
        .channel = ES8389_CHANNELS,
        .channel_mask = 0,
        .sample_rate = ES8389_CODEC_RATE,
        .mclk_multiple = 256,
    };
    int ret = esp_codec_dev_open(state.codec, &sample_info);
    if (ret != ESP_CODEC_DEV_OK)
        return set_error("esp_codec_dev_open failed");

    state.opened = true;
    esp_codec_dev_vol_map_t vol_map[] = {
        {.vol = 0, .db_value = -96.0f},
        {.vol = 1, .db_value = -24.0f},
        {.vol = 25, .db_value = -12.0f},
        {.vol = 50, .db_value = -5.0f},
        {.vol = 100, .db_value = 0.0f},
    };
    esp_codec_dev_vol_curve_t vol_curve = {
        .vol_map = vol_map,
        .count = RG_COUNT(vol_map),
    };
    esp_codec_dev_set_vol_curve(state.codec, &vol_curve);
    esp_codec_dev_set_out_vol(state.codec, state.volume);
    esp_codec_dev_set_out_mute(state.codec, state.muted);
    gpio_set_level(RG_GPIO_SND_AMP_ENABLE, state.muted ? 0 : 1);
    RG_LOGI("ES8389 opened. source_rate=%d codec_rate=%d\n", sample_rate, ES8389_CODEC_RATE);
    return true;
}

static void audio_task(void *arg)
{
    (void)arg;
    rg_audio_frame_t out[ES8389_CHUNK_FRAMES * 2];

    while (state.task_running)
    {
        size_t bytes = 0;
        rg_audio_frame_t *in = (rg_audio_frame_t *)xRingbufferReceive(state.ringbuf, &bytes, pdMS_TO_TICKS(100));
        if (!in)
            continue;

        size_t in_count = bytes / sizeof(rg_audio_frame_t);
        size_t out_count = 0;
        uint32_t source_rate = state.sample_rate > 0 ? state.sample_rate : ES8389_CODEC_RATE;

        for (size_t i = 0; i < in_count; ++i)
        {
            state.resample_accum += ES8389_CODEC_RATE;
            while (state.resample_accum >= source_rate)
            {
                state.resample_accum -= source_rate;
                out[out_count++] = state.muted ? (rg_audio_frame_t){0, 0} :
                    (rg_audio_frame_t){
                        amplify_sample(in[i].left),
                        amplify_sample(in[i].right),
                    };

                if (out_count == RG_COUNT(out))
                {
                    esp_codec_dev_write(state.codec, out, out_count * sizeof(rg_audio_frame_t));
                    out_count = 0;
                    state.write_count++;
                }
            }
        }

        if (out_count)
        {
            int ret = esp_codec_dev_write(state.codec, out, out_count * sizeof(rg_audio_frame_t)); //fix 2026-06-08 log write errors
            if (ret != 0 && state.write_count < 10) {
                RG_LOGW("ES8389 esp_codec_dev_write returned %d\n", ret);
            }
            state.write_count++;
        }

        vRingbufferReturnItem(state.ringbuf, in);
    }

    memset(out, 0, sizeof(out));
    if (state.codec && state.opened)
        esp_codec_dev_write(state.codec, out, sizeof(out));

    vTaskDelete(NULL);
}

static bool driver_init(int device, int sample_rate)
{
    (void)device;
    memset(&state, 0, sizeof(state));
    state.sample_rate = sample_rate;
    state.volume = rg_audio_get_volume();

    gpio_reset_pin(RG_GPIO_SND_AMP_ENABLE);
    gpio_set_direction(RG_GPIO_SND_AMP_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(RG_GPIO_SND_AMP_ENABLE, 0);

    if (!rg_i2c_init())
        return set_error("rg_i2c_init failed");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ES8389_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &state.tx_chan, NULL);
    if (err != ESP_OK)
        return set_error(esp_err_to_name(err));

    // Match Board Manager YAML configuration for ESP32-S31-Korvo-1 //fix 2026-06-08
    // Key: mclk=-1 (no MCLK), slot_bit_width=AUTO, ws_width=16
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = ES8389_CODEC_RATE,  // 48000 Hz //fix 2026-06-08 was sample_rate
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT, //fix 2026-06-08 was 32BIT
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = 16, //fix 2026-06-08 was 32
            .ws_pol = false,
            .bit_shift = false, //fix 2026-06-08 was true
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,  // Board Manager: mclk=-1 (no MCLK) //fix 2026-06-08 was RG_GPIO_SND_I2S_MCLK
            .bclk = RG_GPIO_SND_I2S_BCK,   // GPIO 3
            .ws = RG_GPIO_SND_I2S_WS,      // GPIO 4
            .dout = RG_GPIO_SND_I2S_DATA,  // GPIO 5
            .din = GPIO_NUM_NC,
        },
    };
    err = i2s_channel_init_std_mode(state.tx_chan, &std_cfg);
    if (err != ESP_OK)
        return set_error(esp_err_to_name(err));

    audio_codec_i2c_cfg_t codec_i2c_cfg = {
        .port = ES8389_I2C_PORT,
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = NULL,
    };
    state.ctrl_if = audio_codec_new_i2c_ctrl(&codec_i2c_cfg);
    if (!state.ctrl_if)
        return set_error("audio_codec_new_i2c_ctrl failed");

    // Match Board Manager YAML: mclk_enabled=false, pa active_level=1 //fix 2026-06-08
    es8389_codec_cfg_t codec_cfg = {
        .ctrl_if = state.ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = RG_GPIO_SND_AMP_ENABLE,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = false,  // Board Manager: mclk_enabled=false //fix 2026-06-08 was true
        .mclk_div = 256,
    };
    state.codec_if = es8389_codec_new(&codec_cfg);
    if (!state.codec_if)
        return set_error("es8389_codec_new failed");

    audio_codec_i2s_cfg_t codec_i2s_cfg = {
        .port = ES8389_I2S_PORT,
        .tx_handle = state.tx_chan,
        .rx_handle = NULL,
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };
    state.data_if = audio_codec_new_i2s_data(&codec_i2s_cfg);
    if (!state.data_if)
        return set_error("audio_codec_new_i2s_data failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = state.codec_if,
        .data_if = state.data_if,
    };
    state.codec = esp_codec_dev_new(&dev_cfg);
    if (!state.codec)
        return set_error("esp_codec_dev_new failed");

    esp_codec_set_disable_when_closed(state.codec, false);

    if (!open_codec(sample_rate))
        return false;

    state.ringbuf = xRingbufferCreate(
        ES8389_CHUNK_FRAMES * sizeof(rg_audio_frame_t) * ES8389_RING_CHUNKS,
        RINGBUF_TYPE_NOSPLIT);
    if (!state.ringbuf)
        return set_error("xRingbufferCreate failed");

    state.task_running = true;
    if (xTaskCreatePinnedToCore(audio_task, "es8389_audio", ES8389_TASK_STACK, NULL,
            ES8389_TASK_PRIO, &state.task, 1) != pdPASS)
    {
        state.task_running = false;
        return set_error("xTaskCreatePinnedToCore failed");
    }

    return true;
}

static bool driver_deinit(void)
{
    state.task_running = false;
    if (state.task)
    {
        vTaskDelay(pdMS_TO_TICKS(120));
        state.task = NULL;
    }
    if (state.ringbuf)
    {
        vRingbufferDelete(state.ringbuf);
        state.ringbuf = NULL;
    }
    if (state.codec)
    {
        esp_codec_dev_set_out_mute(state.codec, true);
        esp_codec_dev_close(state.codec);
        esp_codec_dev_delete(state.codec);
    }
    if (state.codec_if)
        audio_codec_delete_codec_if(state.codec_if);
    if (state.data_if)
        audio_codec_delete_data_if(state.data_if);
    if (state.ctrl_if)
        audio_codec_delete_ctrl_if(state.ctrl_if);
    if (state.tx_chan)
    {
        i2s_del_channel(state.tx_chan);
        state.tx_chan = NULL;
    }
    gpio_set_level(RG_GPIO_SND_AMP_ENABLE, 0);
    gpio_reset_pin(RG_GPIO_SND_AMP_ENABLE);
    memset(&state, 0, sizeof(state));
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    if (!state.codec || !state.opened)
        return false;

    int peak = 0;

    state.submit_count++;

    for (size_t i = 0; i < count; ++i)
    {
        int left = frames[i].left < 0 ? -frames[i].left : frames[i].left;
        int right = frames[i].right < 0 ? -frames[i].right : frames[i].right;
        if (left > peak)
            peak = left;
        if (right > peak)
            peak = right;
    }

    if (state.ringbuf)
    {
        size_t bytes = count * sizeof(rg_audio_frame_t);
        if (xRingbufferSend(state.ringbuf, (void *)frames, bytes, pdMS_TO_TICKS(120)) != pdTRUE)
        {
            RG_LOGW("ES8389 queue full, submit=%u frames=%u peak=%d\n",
                (unsigned)state.submit_count, (unsigned)count, peak);
        }
    }

    if (state.submit_count <= 3 || (state.submit_count % 60) == 0) //fix 2026-06-08 was % 240
    {
        RG_LOGI("ES8389 audio submit=%u in_frames=%u source_rate=%u muted=%d peak=%d first=%d/%d write=%u\n",
            (unsigned)state.submit_count, (unsigned)count, (unsigned)state.sample_rate,
            state.muted, peak, frames[0].left, frames[0].right, (unsigned)state.write_count);
    }

    return true;
}

static bool driver_set_mute(bool mute)
{
    state.muted = mute;
    if (state.codec)
        esp_codec_dev_set_out_mute(state.codec, mute);
    gpio_set_level(RG_GPIO_SND_AMP_ENABLE, mute ? 0 : 1);
    RG_LOGI("ES8389 mute=%d\n", mute);
    return true;
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    if (state.codec)
        esp_codec_dev_set_out_vol(state.codec, volume);
    RG_LOGI("ES8389 volume=%d\n", volume);
    return true;
}

static bool driver_set_sample_rate(int sample_rate)
{
    if (sample_rate == state.sample_rate)
        return true;
    state.sample_rate = sample_rate;
    state.resample_accum = 0;
    RG_LOGI("ES8389 source sample_rate=%d\n", sample_rate);
    return true;
}

static const char *driver_get_error(void)
{
    return state.last_error;
}

const rg_audio_driver_t rg_audio_driver_es8389 = {
    .name = "es8389",
    .init = driver_init,
    .deinit = driver_deinit,
    .submit = driver_submit,
    .set_mute = driver_set_mute,
    .set_volume = driver_set_volume,
    .set_sample_rate = driver_set_sample_rate,
    .get_error = driver_get_error,
};

#endif
