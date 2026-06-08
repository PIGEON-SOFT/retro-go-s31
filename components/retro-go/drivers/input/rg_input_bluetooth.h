#pragma once //fix 2026-06-08 new file

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RG_BT_TRANSPORT_BLE = 0,
    RG_BT_TRANSPORT_CLASSIC = 1,
    RG_BT_TRANSPORT_DUAL = 2,
} rg_bt_transport_t;

typedef struct {
    uint8_t bda[6];
    char name[64];
    rg_bt_transport_t transport;
    int rssi;
    bool paired;
} rg_bt_device_info_t;

typedef void (*rg_bt_scan_callback_t)(const rg_bt_device_info_t *device, void *user_data);

void rg_input_bluetooth_init(rg_bt_transport_t transport);
void rg_input_bluetooth_deinit(void);
uint32_t rg_input_bluetooth_read(void);
bool rg_input_bluetooth_is_connected(void);
const char *rg_input_bluetooth_get_state_str(void);
int rg_input_bluetooth_scan(int duration_sec, rg_bt_scan_callback_t callback, void *user_data);
bool rg_input_bluetooth_connect(const uint8_t *bda, rg_bt_transport_t transport);
void rg_input_bluetooth_disconnect(void);
bool rg_input_bluetooth_get_connected_device(rg_bt_device_info_t *info);
bool rg_input_bluetooth_save_device(const rg_bt_device_info_t *device);
bool rg_input_bluetooth_load_device(rg_bt_device_info_t *device);
void rg_input_bluetooth_clear_saved_device(void);

#ifdef __cplusplus
}
#endif
