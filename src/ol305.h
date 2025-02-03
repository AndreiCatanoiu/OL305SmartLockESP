#ifndef __OL305_H__
#define __OL305_H__

#include "nvs.h"

typedef enum
{
	OL305_STATE_ENABLE,
	OL305_STATE_DISABLE,
	OL305_STATE_SHUTDOWN,
	OL305_STATE_MAX
}OL305_STATE;

typedef enum 
{
    BLE_KEY = 0x01,
    UNLOCK = 0x05,
    CMD_ERROR = 0x10,
    LOCK = 0x15,
    BLE_KEY_SETTING = 0x20,
    QUERY_INFO = 0x31,
    OBTAIN_LAST_USAGE = 0x51,
    DELETE_LAST_USAGE = 0x52,
    LOCK_SETTINGS = 0x61,
    REGISTER_RFID = 0x85,
    DELETE_RFID = 0x86,
    GET_RFID = 0x87,
} ol305b_cmd;

void ol305_recive_message(uint8_t *data, uint16_t len);
void set_ol305_mac_addr(uint8_t *ol305_mac_addr, uint16_t len);
void ol305_task(void *pvParameters);
void ol305_unlock();
void ol305_query();
void ol305_read_rfid();
void ol305_delete_rfid();
void ol305_settings(); 
bool is_ol305_connected();
void ol305_control(OL305_STATE state, uint8_t wait, uint32_t timeout_ms);
void ol305_disconnect();
void set_ol305_ble_password(const char *password);

#endif