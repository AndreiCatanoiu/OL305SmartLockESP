#ifndef __BLE_CONNECTION_H__
#define __BLE_CONNECTION_H__

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

typedef enum
{
    SERVICE_UUID,
    WRITE_UUID,
    NOTIFY_UUID,
    NOTIFY_DESCR_UUID,
    LAST_UUID,
}uuid_type;

void ble_init();
void ble_deinit();
void set_uuid(uint8_t *uuid, uuid_type type);
void set_target_mac(uint8_t* new_mac_addr,uint16_t mac_len);
bool is_ble_connected();
void ble_write(uint8_t *data, uint16_t len);

#endif