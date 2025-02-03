#include <string.h>
#include "ble_connection.h"
#include "ol305.h"
#include "esp_log.h"
#include "esp_timer.h"

#define MAX_MSG_LEN 22
const static char *TAG = "OL305";

static uint8_t service_uuid[] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e};
static uint8_t write_uuid[] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e};
static uint8_t notify_uuid[] = {0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e};
static uint8_t notify_decr_uuid[] = {0x29, 0x02};

typedef enum
{
	INVALID = 0,
	CONNECTING,
	CONNECTED,
	DISCONNECTING,
	DISCONNECTED
} OL305_STATES;

typedef enum
{
    INVALID_MESSAGE = 0,
    UNLOCK_MESSAGE,
    QUERY_INFO_MESSAGE,
    UNLOCK_RESPONSE_MESSAGE,
    LOCK_RESPONSE_MESSAGE,
    REGISTER_RFID_MESSAGE,
    DELETE_RFID_MESSAGE,
    LOCK_SETTINGS_MESSAGE,
} OL305_MSG_TYPE;

typedef struct
{
    OL305_MSG_TYPE msg_type;
    uint16_t stx;
    uint8_t len;
    uint8_t rand;
    uint8_t key;
    uint8_t cmd;
    uint8_t data[MAX_MSG_LEN - 5];
    uint8_t crc;
} Message_OL305B_t;

typedef struct
{
	OL305_STATE new_state;
	OL305_STATES state;
	uint8_t mac[6];
    uint8_t status; //invalid -> 0x00; unlocked -> 0x01; locked -> 0x02
    uint8_t expected_status; //invalid -> 0x00; unlocked -> 0x01; locked -> 0x02
    int battery_voltage;
    char password[8];
} OL305Details_t;

Message_OL305B_t message_to_send=
{
    .stx = 0xa3a4,
};
static OL305Details_t ol305_details;

unsigned char CRC8Table[]=
{
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};

static unsigned char crc_calc(unsigned char *pucFrame, char usLen)
{
    unsigned char crc8 = 0;
    
    while(usLen--)
    {
        crc8 = CRC8Table[crc8^*(pucFrame++)];
    }

    return(crc8);
}

void set_ol305_ble_password(const char *password)
{
    if (4 > strlen(password) || 8 < strlen(password))
    {
        ESP_LOGE(TAG,"Wrong len for password!");
        return;
    }

    memcpy(ol305_details.password,password,strlen(password));
}

void set_ol305_mac_addr(uint8_t *ol305_mac_addr, uint16_t len)
{
    if (len != 6)
    {
        ESP_LOGE(TAG,"Wrong MAC addr for OL305");
        return;
    }
    memcpy(ol305_details.mac,ol305_mac_addr,len);
}

static void ol305_task_events(OL305_STATES new_state)
{
	if (ol305_details.state == new_state)
	{
		ESP_LOGI(TAG, "Same state received %d", new_state);
		return;
	}
	switch (new_state)
	{
        case CONNECTING:
            ESP_LOGI(TAG, "OL305 connecting...");
            ol305_details.state = new_state;
            break;
        case CONNECTED:
            ESP_LOGI(TAG, "OL305 connected");
            ol305_details.state = new_state;
            break;
        case DISCONNECTING:
            ESP_LOGI(TAG, "OL305 disconnecting...");
            ol305_details.state = new_state;
            break;
        case DISCONNECTED:
            ESP_LOGI(TAG, "OL305 disconnected");
            ol305_details.state = new_state;
            break;
        default:
            ESP_LOGI(TAG, "Unknown state received %d", new_state);
            break;
	}
}

static void ol305_encode_key_message(const char* password)
{
    message_to_send.len = strlen(password); 
    message_to_send.key = 0x00; 
    message_to_send.cmd = BLE_KEY;
    memcpy(message_to_send.data, (uint8_t*)password, message_to_send.len);
}

static void ol305_encode_unlock_message(uint8_t control_cmd, int64_t user_id, int64_t operation_timestamp, uint8_t unlock_status)
{
    message_to_send.len = 0x0a;
    message_to_send.cmd = UNLOCK;

    uint8_t *id = (uint8_t*)&user_id;
    uint8_t *timestamp = (uint8_t*)&operation_timestamp;

    uint8_t data[] = {control_cmd, id[3], id[2], id[1], id[0], timestamp[3], timestamp[2], timestamp[1], timestamp[0], unlock_status};
    memcpy(message_to_send.data, data, message_to_send.len); 
}

static void ol305_encode_query_message()
{
    message_to_send.len = 0x01;
    message_to_send.cmd = QUERY_INFO; 
    message_to_send.data[0] = 0x01; 
}

static void ol305_encode_read_rfid_message()
{
    message_to_send.len = 0x01; 
    message_to_send.cmd = REGISTER_RFID;
    message_to_send.data[0] = 0x01;
}

static void ol305_encode_delete_rfid_message(const uint8_t *data, uint16_t len)
{
    message_to_send.len = 0x08; 
    message_to_send.cmd = DELETE_RFID; 
    if (message_to_send.len == len)
    {
        memcpy(message_to_send.data, data, message_to_send.len);
    }
    else
    {
        ESP_LOGE(TAG,"WRONG LEN FOR RFID CARD, TRY AGAIN");
        return;
    }
}

static void ol305_encode_settings_message(uint8_t bluetooth_unlock, uint8_t button_unlock, uint8_t RFID_unlock)
{
    message_to_send.len = 0x04; 
    message_to_send.cmd = LOCK_SETTINGS; 
    if (2 < bluetooth_unlock || 2 < button_unlock || 2 < RFID_unlock)
        return;
        
    message_to_send.data[0] = bluetooth_unlock; 
    message_to_send.data[1] = button_unlock; 
    message_to_send.data[2] = RFID_unlock; 
}

static void ol305_encode_response_message(uint8_t response)
{
    message_to_send.len = 0x01;
    if (UNLOCK == response)
        message_to_send.cmd = UNLOCK;
    else if (LOCK == response)
        message_to_send.cmd = LOCK;
    message_to_send.data[0] = 0x02;
}

static void ol305_deinit_message()
{
    message_to_send.msg_type = INVALID_MESSAGE;
    message_to_send.len = 0x00;  
    message_to_send.cmd = 0x00;
    for (uint8_t i = 0; i < MAX_MSG_LEN - 5; i++)
        message_to_send.data[i] = 0x00;
}

static void ol305_details_deinit()
{
	ol305_details.state = INVALID;
    ol305_details.status = 0x00; 
    ol305_details.battery_voltage = 0;
}

static void ol305_send_message()
{
    uint8_t data_to_write[MAX_MSG_LEN]; 
    data_to_write[0]= (message_to_send.stx >> 8) & 0xFF;
    data_to_write[1]= message_to_send.stx & 0xFF;

    if (0x00 == message_to_send.cmd)
    {
        return;
    }
    else
    {
        int64_t time = esp_timer_get_time() / 1000;
        srand(time);
        message_to_send.rand =  rand() % 256;
        data_to_write[2] = message_to_send.len;              
        data_to_write[3] = 0x32 + message_to_send.rand;     
        
        uint8_t temp_data[2 + message_to_send.len];
        temp_data[0] = message_to_send.key;
        temp_data[1] = message_to_send.cmd;
        memcpy(&temp_data[2], message_to_send.data, message_to_send.len);

        for (uint8_t i = 0; i < (2 + message_to_send.len); i++) 
        {
            data_to_write[4 + i] = temp_data[i] ^ message_to_send.rand;
        }

        message_to_send.crc = crc_calc(data_to_write, 6 + message_to_send.len);
        data_to_write[6 + message_to_send.len] = message_to_send.crc;

        ble_write(data_to_write, 7 + message_to_send.len);
        ol305_deinit_message();
    }
}

void ol305_recive_message(uint8_t *data, uint16_t len)
{    
    Message_OL305B_t message_recived;
    if (7 >= len)
    {
        ESP_LOGE(TAG, "Invalid data recived");
        return; 
    }

    message_recived.crc = data[6 + data[2]];
    if (crc_calc(data, len-1) != message_recived.crc)
    {
        ESP_LOGE(TAG,"Invalid CRC recived");
        return;
    }

    message_recived.stx = (uint16_t)((data[0] << 8) + (data[1] & 0x00ff));
    if (0xa3a4 != message_recived.stx)
    {
        ESP_LOGE(TAG,"Invalid STX recived");
        return;
    }

    message_recived.len = data[2];
    if (MAX_MSG_LEN-5 <= message_recived.len)
    {
        ESP_LOGE(TAG,"The length of the message is to big");
        return;
    }

    message_recived.rand = data[3] - 0x32;
    message_recived.key = data[4] ^ message_recived.rand;
    if (message_recived.key != message_to_send.key && 0x00 != message_to_send.key)
    {
        ESP_LOGE(TAG,"Invalid key recived");
        return;
    }

    message_recived.cmd = data[5] ^ message_recived.rand;
    for (uint8_t i = 0; i < message_recived.len; i++)
    {
        message_recived.data[i] = data[6 + i] ^ message_recived.rand;
    }
    
    switch (message_recived.cmd)
    {   
        case BLE_KEY:
            if (message_recived.key == message_recived.data[1])
            {
                ESP_LOGI(TAG,"Correct BLE Key");
                message_to_send.key=message_recived.key;
                ol305_task_events(CONNECTED);
            }
            else
                ESP_LOGE(TAG,"Error trying to get the BLE Key");
            break;
        
        case UNLOCK:
            if (0x01 == message_recived.data[0])
            {
                message_to_send.msg_type = UNLOCK_RESPONSE_MESSAGE;
            }
            else if (0x02 == message_recived.data[0])
                ESP_LOGE(TAG,"Unlock failed");
            break;
        
        case CMD_ERROR:
            if (0x01 == message_recived.data[0])
                ESP_LOGE(TAG,"CRC authentication error");
            else if (0x02 == message_recived.data[0])
            {
                ESP_LOGE(TAG,"Bluetooth KEY not obtained");
                ol305_task_events(DISCONNECTING);
            }
            else if (0x03 == message_recived.data[0])
            {
                ESP_LOGE(TAG,"Received Bluetooth KEY, but Bluetooth KEY error");
                ol305_task_events(DISCONNECTING);
            }
            break;
        
        case LOCK:
            if (0x01 == message_recived.data[0])
            {
                ESP_LOGI(TAG,"Successfully locked");
                ol305_details.expected_status = 0x02;
                message_to_send.msg_type = LOCK_RESPONSE_MESSAGE;
            }
            else if (0x02 == message_recived.data[0])
                ESP_LOGE(TAG,"Lock failed");
            break;
        
        case QUERY_INFO:
            uint16_t battery_voltage = (message_recived.data[0] << 8) | message_recived.data[1];
            ol305_details.battery_voltage = ((int)battery_voltage)* 10;

            if (1 == ((message_recived.data[2] >> 1) & 1))  
                ol305_details.status = 0x02; //locked

            if (1 == ((message_recived.data[2] >> 0) & 1))
                ol305_details.status = 0x01; //unlocked

            break;
        
        case REGISTER_RFID:
            if (0x00 == message_recived.data[0])
                ESP_LOGD(TAG,"Start reading");
            else if (0x01 == message_recived.data[0])
            {
                ESP_LOGI(TAG,"Read card successfully, valid card number : ");
                uint8_t card_readed[8];
                memcpy(card_readed, &message_recived.data+1, 8);
                ESP_LOGI(TAG,"RFID registered : ");
                ESP_LOG_BUFFER_HEX(TAG, card_readed, sizeof(card_readed));
            }
            else if (0x02 == message_recived.data[0])
                ESP_LOGE(TAG,"Adding failed");
            else if (0x03 == message_recived.data[0])
                ESP_LOGW(TAG,"Card already exists");
            break;

        case DELETE_RFID:
            if (0x00 == message_recived.data[0])
                ESP_LOGE(TAG,"Delete failed/Card doesn't exist");
            else if (0x01 == message_recived.data[0])
                ESP_LOGI(TAG,"Deleted successfully");
            break;

        case LOCK_SETTINGS:
            for (uint8_t i = 0; i < 3; i++)
            {
                switch (i)
                {
                    case 0:
                        ESP_LOGI(TAG,"BLE Unlock : ");
                        break;

                    case 1:
                        ESP_LOGI(TAG,"Button Unlock : ");
                        break;

                    case 2:
                        ESP_LOGI(TAG,"RFID Unlock : ");
                        break;
                    
                    default:
                        break;
                }

                if (0x01 == message_recived.data[i])
                    ESP_LOGI(TAG,"OFF");
                else if (0x02 == message_recived.data[i])
                    ESP_LOGI(TAG,"ON");
            }
            break;
        default:
            ESP_LOGI(TAG,"Invalid command recived");
            break;
    }
}

static void ol305_connect()
{
    while (OL305_STATE_ENABLE != ol305_details.new_state)
    {
        ESP_LOGW(TAG,"OL305 not enabled set!");
        vTaskDelay (250 / portTICK_PERIOD_MS);
        continue;
    }

    set_uuid(service_uuid, SERVICE_UUID);
    set_uuid(write_uuid, WRITE_UUID);
    set_uuid(notify_uuid, NOTIFY_UUID);
    set_uuid(notify_decr_uuid, NOTIFY_DESCR_UUID);
    set_target_mac(ol305_details.mac, sizeof(ol305_details.mac));
    ble_init();

    while (true != is_ble_connected())
    {
        vTaskDelay (250 / portTICK_PERIOD_MS);
        continue;
    }
                
    ol305_encode_key_message(ol305_details.password);
    ol305_send_message();
}

static void ol305_status_check()
{
    ol305_encode_query_message();
    ol305_send_message();
    if (ol305_details.status != ol305_details.expected_status && 0 != ol305_details.expected_status)
    {
        if (ol305_details.status == 0x01)
            ESP_LOGW(TAG,"Warning : OL305 should be locked but it is unlocked");
        else if (ol305_details.status == 0x02)
            ESP_LOGW(TAG,"Warning : OL305 should be unlocked but it is locked");
    }
    else if (ol305_details.status != ol305_details.expected_status && 0 == ol305_details.expected_status)
        ol305_details.expected_status = ol305_details.status;
}

void ol305_task(void *pvParameters)
{
	ESP_LOGI(TAG, "Task started!");
	while (1)
	{
		switch (ol305_details.state)
		{
            case INVALID:
                if (ol305_details.new_state == OL305_STATE_ENABLE)
                    ol305_task_events(CONNECTING);
                else
                    ol305_task_events(DISCONNECTED);
                break;

            case CONNECTING:
                if (ol305_details.new_state == OL305_STATE_DISABLE)
                {
                    ESP_LOGI(TAG, "Connecting to Disable");
                    ol305_task_events(DISCONNECTING);
                    continue;
                }
                if (ol305_details.new_state == OL305_STATE_SHUTDOWN)
                {
                    ESP_LOGI(TAG, "Connecting to ShutDown");
                    ol305_task_events(DISCONNECTING);
                    continue;
                }
                ol305_connect();
                break;

            case CONNECTED:
                if (ol305_details.new_state == OL305_STATE_DISABLE)
                {
                    ESP_LOGI(TAG, "Connected to Disable");
                    ol305_task_events(DISCONNECTING);
                    continue;
                }
                if (ol305_details.new_state == OL305_STATE_SHUTDOWN)
                {
                    ESP_LOGI(TAG, "Connected to ShutDown");
                    ol305_task_events(DISCONNECTING);
                    continue;
                }

                switch (message_to_send.msg_type)
                {
                    case UNLOCK_MESSAGE:
                        uint8_t control_cmd = 0x01;
                        int64_t user_id = 0x01;
                        int64_t operation_timestamp = esp_timer_get_time() / 1000;
                        uint8_t unlock_status = 0x00;
                        
                        ol305_encode_query_message();
                        ol305_send_message();
                        if (ol305_details.status != 0x01)
                        {
                            while (ol305_details.status != 0x01)
                            {
                                ol305_encode_unlock_message(control_cmd, user_id, operation_timestamp, unlock_status);
                                ol305_send_message();
                                ol305_encode_query_message();
                                ol305_send_message();
                                vTaskDelay(2500 / portTICK_PERIOD_MS);
                            }
                            ESP_LOGI(TAG,"Successfully unlocked");
                            ol305_details.expected_status = 0x01;
                        }
                        else
                            ESP_LOGW(TAG,"OL305 already unlocked!");
                        break;

                    case QUERY_INFO_MESSAGE:
                        ol305_encode_query_message();
                        ol305_send_message();
                        while (ol305_details.status == 0x00)
                        {
                            ol305_encode_query_message();
                            ol305_send_message();
                            vTaskDelay(150 / portTICK_PERIOD_MS);
                        }

                        if (ol305_details.status == 0x02)
                            ESP_LOGI(TAG,"Status : locked");
                        else if (ol305_details.status == 0x01)
                            ESP_LOGI(TAG,"Status : unlocked");

                        ESP_LOGI(TAG,"Battery voltage : %d mV", ol305_details.battery_voltage);
                        ol305_details.status = 0x00;
                        break;

                    case UNLOCK_RESPONSE_MESSAGE: 
                        ol305_encode_response_message(UNLOCK);
                        ol305_send_message();
                        break;

                    case LOCK_RESPONSE_MESSAGE:
                        ol305_encode_response_message(LOCK);
                        ol305_send_message();
                        break;

                    case REGISTER_RFID_MESSAGE:
                        ol305_encode_read_rfid_message();
                        ol305_send_message();
                        break;

                    case DELETE_RFID_MESSAGE:
                        uint8_t all_nfc_tokens[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                        ol305_encode_delete_rfid_message(all_nfc_tokens,sizeof(all_nfc_tokens));
                        ol305_send_message();
                        break;
                    
                    case LOCK_SETTINGS_MESSAGE:
                        ol305_encode_settings_message(0x02, 0x01, 0x01);
                        ol305_send_message();
                        break;

                    default:
                        break;
                }
                message_to_send.msg_type = INVALID_MESSAGE;
                ol305_status_check();
                break;

            case DISCONNECTING:
                ol305_deinit_message();
                ol305_details_deinit();
                message_to_send.key = 0x00;
                ble_deinit();
                ol305_task_events(DISCONNECTED);
                if (ol305_details.new_state == OL305_STATE_SHUTDOWN)
                {
                    ESP_LOGI(TAG, "stoping task");
                    vTaskDelete(NULL);
                }
                break;

            case DISCONNECTED:
                if (ol305_details.new_state == OL305_STATE_DISABLE)
                {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }
                if (ol305_details.new_state == OL305_STATE_SHUTDOWN)
                {
                    ESP_LOGI(TAG, "stoping task");
                    vTaskDelete(NULL);
                }
                ol305_task_events(CONNECTING);
                break;

            default:
                ESP_LOGE(TAG, "Task went wrong!");
                break;

		}
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	ESP_LOGI(TAG, "stoping task");
	vTaskDelete(NULL);
}

void ol305_disconnect()
{
    ol305_task_events(DISCONNECTING);
}

void ol305_unlock()
{
    message_to_send.msg_type = UNLOCK_MESSAGE;
}

void ol305_query()
{
    message_to_send.msg_type = QUERY_INFO_MESSAGE;
}

void ol305_read_rfid()
{
    message_to_send.msg_type = REGISTER_RFID_MESSAGE;
}

void ol305_delete_rfid()
{
    message_to_send.msg_type = DELETE_RFID_MESSAGE;
}

void ol305_settings()
{
    message_to_send.msg_type = LOCK_SETTINGS_MESSAGE;
}

bool is_ol305_connected()
{
    return ol305_details.state == CONNECTED;
}

void ol305_control(OL305_STATE state, uint8_t wait, uint32_t timeout_ms)
{
	switch (state)
	{
        case OL305_STATE_ENABLE:
            ESP_LOGI(TAG, "Switch to Enable");
            break;
        case OL305_STATE_DISABLE:
            ESP_LOGI(TAG, "Switch to Disable");
            break;
        case OL305_STATE_SHUTDOWN:
            ESP_LOGI(TAG, "Switch to ShutDown");
            break;
        default:
            return;
            break;
	}
	
	ol305_details.new_state = state;
	if (!wait)
		return;
	uint32_t timeout_timer = esp_timer_get_time() + timeout_ms;
	do
	{
		const uint8_t enable_done = ol305_details.state == CONNECTED && ol305_details.new_state == OL305_STATE_ENABLE;
		const uint8_t disable_shutdown_done = ol305_details.state == DISCONNECTED && (ol305_details.new_state == OL305_STATE_DISABLE || ol305_details.new_state == OL305_STATE_SHUTDOWN);
		if (enable_done || disable_shutdown_done)
		{
			ESP_LOGI(TAG, "Job done");
			return;
		}
		vTaskDelay(1);
	} while (timeout_timer > esp_timer_get_time());
	ESP_LOGI(TAG, "Job timeout");
}