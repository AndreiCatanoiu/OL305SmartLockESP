#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "ble_connection.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "ol305.h"
#include "freertos/FreeRTOS.h"

#define INVALID_HANDLE 0
const static char *TAG = "BLE_CONNECTION";

esp_bd_addr_t TARGET_MAC;
static bool ble_connection = false;
static bool get_server = false;
static bool firts_time = true;
static esp_gattc_char_elem_t *char_elem_result = NULL;
static esp_gattc_char_elem_t *write_elem_result = NULL;
static esp_gattc_descr_elem_t *descr_elem_result = NULL;

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

typedef struct  
{
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    uint16_t write_handle;
    esp_bd_addr_t remote_bda;
}gattc_profile_inst;

esp_bt_uuid_t remote_filter_service_uuid;
esp_bt_uuid_t remote_filter_char_uuid;
esp_bt_uuid_t notify_uuid;
esp_bt_uuid_t notify_decr_uuid;

static esp_ble_scan_params_t ble_scan_params =
{
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE
};

gattc_profile_inst gl_profile_tab =
{
    .gattc_cb = gattc_profile_event_handler,
    .gattc_if = ESP_GATT_IF_NONE,
};

bool is_ble_connected()
{
    return ble_connection;
}

void ble_deinit()
{
    esp_ble_gattc_close(gl_profile_tab.gattc_if, gl_profile_tab.conn_id);
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_ble_gattc_app_unregister(gl_profile_tab.gattc_if);
    esp_ble_gattc_unregister_for_notify(gl_profile_tab.gattc_if, gl_profile_tab.remote_bda, gl_profile_tab.char_handle);
    esp_ble_gattc_app_unregister(INVALID_HANDLE);

    ESP_LOGI(TAG, "BLE deinitialized");
}

static void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;
    switch (event)
    {
        case ESP_GATTC_REG_EVT:
            ESP_LOGI(TAG, "REG_EVT");
            esp_err_t scan_ret = esp_ble_gap_set_scan_params(&ble_scan_params);
            if (scan_ret)
            {
                ESP_LOGE(TAG, "set scan params error, error code = %x", scan_ret);
            }
            break;

        case ESP_GATTC_CONNECT_EVT:
            ESP_LOGI(TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", p_data->connect.conn_id, gattc_if);
            gl_profile_tab.conn_id = p_data->connect.conn_id;
            memcpy(gl_profile_tab.remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "REMOTE BDA:");
            esp_log_buffer_hex(TAG, gl_profile_tab.remote_bda, sizeof(esp_bd_addr_t));
            esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req(gattc_if, p_data->connect.conn_id);
            if (mtu_ret)
            {
                ESP_LOGE(TAG, "config MTU error, error code = %x", mtu_ret);
            }
            break;
        
        case ESP_GATTC_OPEN_EVT:
            if (param->open.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "open failed, status %d", p_data->open.status);
                break;
            }
            ESP_LOGI(TAG, "open success");
            break;

        case ESP_GATTC_DIS_SRVC_CMPL_EVT:
            if (param->dis_srvc_cmpl.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "discover service failed, status %d", param->dis_srvc_cmpl.status);
                break;
            }
            ESP_LOGI(TAG, "discover service complete conn_id %d", param->dis_srvc_cmpl.conn_id);
            esp_ble_gattc_search_service(gattc_if, param->dis_srvc_cmpl.conn_id, NULL);
            break;

        case ESP_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "config mtu failed, error status = %x", param->cfg_mtu.status);
            }
            ESP_LOGI(TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
            break;

        case ESP_GATTC_SEARCH_RES_EVT:
            ESP_LOGI(TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
            ESP_LOGI(TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);

            if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_128)
            {
                bool comp = true;
                for (int i = 0; i < ESP_UUID_LEN_128; i++)
                {
                    if (p_data->search_res.srvc_id.uuid.uuid.uuid128[i] != remote_filter_service_uuid.uuid.uuid128[i])
                    {
                        comp = false;
                        break;
                    }
                }

                if (comp == true)
                {
                    ESP_LOGI(TAG, "service found");
                    get_server = true;
                    gl_profile_tab.service_start_handle = p_data->search_res.start_handle;
                    gl_profile_tab.service_end_handle = p_data->search_res.end_handle;
                }
            }
            break;

        case ESP_GATTC_SEARCH_CMPL_EVT:
            if (p_data->search_cmpl.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
                break;
            }

            if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE)
            {
                ESP_LOGI(TAG, "Get service information from remote device");
            }
            else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH)
            {
                ESP_LOGI(TAG, "Get service information from flash");
            }
            else
            {
                ESP_LOGI(TAG, "unknown service source");
            }

            ESP_LOGI(TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
            if (get_server)
            {
                uint16_t count = 0;
                esp_gatt_status_t status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                        p_data->search_cmpl.conn_id,
                                                                        ESP_GATT_DB_CHARACTERISTIC,
                                                                        gl_profile_tab.service_start_handle,
                                                                        gl_profile_tab.service_end_handle,
                                                                        INVALID_HANDLE,
                                                                        &count);
                if (status != ESP_GATT_OK)
                {
                    ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
                    break;
                }

                if (count > 0)
                {
                    char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                    if (!char_elem_result)
                    {
                        ESP_LOGE(TAG, "gattc no mem");
                        break;
                    }
                    else
                    {
                        status = esp_ble_gattc_get_char_by_uuid(gattc_if,
                                                                p_data->search_cmpl.conn_id,
                                                                gl_profile_tab.service_start_handle,
                                                                gl_profile_tab.service_end_handle,
                                                                (esp_bt_uuid_t)notify_uuid,
                                                                char_elem_result,
                                                                &count);
                        if (status != ESP_GATT_OK)
                        {
                            ESP_LOGE(TAG, "esp_ble_gattc_get_char_by_uuid error");
                            free(char_elem_result);
                            char_elem_result = NULL;
                            break;
                        }

                        if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY))
                        {
                            gl_profile_tab.char_handle = char_elem_result[0].char_handle;
                            esp_ble_gattc_register_for_notify(gattc_if, gl_profile_tab.remote_bda, char_elem_result[0].char_handle);
                        }
                    }

                    free(char_elem_result);
                    write_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);

                    if (!write_elem_result)
                    {
                        ESP_LOGE(TAG, "gattc no mem");
                        break;
                    }
                    else
                    {
                        status = esp_ble_gattc_get_char_by_uuid(gattc_if,
                                                                p_data->search_cmpl.conn_id,
                                                                gl_profile_tab.service_start_handle,
                                                                gl_profile_tab.service_end_handle,
                                                                remote_filter_char_uuid,
                                                                write_elem_result,
                                                                &count);
                        if (status != ESP_GATT_OK)
                        {
                            ESP_LOGE(TAG, "esp_ble_gattc_get_char_by_uuid error");
                            free(write_elem_result);
                            write_elem_result = NULL;
                            break;
                        }

                        if (count > 0 && (write_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_WRITE))
                        {
                            gl_profile_tab.write_handle = write_elem_result[0].char_handle;
                        }
                    }
                    free(write_elem_result);
                }
                else
                {
                    ESP_LOGE(TAG, "no char found");
                }
            }
            break;

        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            ESP_LOGI(TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
            if (p_data->reg_for_notify.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
            }
            else
            {
                uint16_t count = 0;
                uint16_t notify_en = 1;
                esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count(gattc_if,
                                                                            gl_profile_tab.conn_id,
                                                                            ESP_GATT_DB_DESCRIPTOR,
                                                                            gl_profile_tab.service_start_handle,
                                                                            gl_profile_tab.service_end_handle,
                                                                            gl_profile_tab.char_handle,
                                                                            &count);
                if (ret_status != ESP_GATT_OK)
                {
                    ESP_LOGE(TAG, "esp_ble_gattc_get_attr_count error");
                    break;
                }

                if (count > 0)
                {
                    descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                    if (!descr_elem_result)
                    {
                        ESP_LOGE(TAG, "malloc error, gattc no mem");
                        break;
                    }
                    else
                    {
                        ret_status = esp_ble_gattc_get_descr_by_uuid(gattc_if,
                                                                    gl_profile_tab.conn_id,
                                                                    gl_profile_tab.service_start_handle,
                                                                    gl_profile_tab.service_end_handle,
                                                                    notify_uuid,
                                                                    notify_decr_uuid,
                                                                    descr_elem_result,
                                                                    &count);
                        if (ret_status != ESP_GATT_OK)
                        {
                            ESP_LOGE(TAG, "esp_ble_gattc_get_descr_by_uuid error");
                            free(descr_elem_result);
                            descr_elem_result = NULL;
                            break;
                        }

                        if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG)
                        {
                            ret_status = esp_ble_gattc_write_char_descr(gattc_if,
                                                                        gl_profile_tab.conn_id,
                                                                        descr_elem_result[0].handle,
                                                                        sizeof(notify_en),
                                                                        (uint8_t *)&notify_en,
                                                                        ESP_GATT_WRITE_TYPE_RSP,
                                                                        ESP_GATT_AUTH_REQ_NONE);
                        }

                        if (ret_status != ESP_GATT_OK)
                        {
                            ESP_LOGE(TAG, "esp_ble_gattc_write_char_descr error");
                        }
                        free(descr_elem_result);
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "decsr not found");
                }
            }
            break;
        
        case ESP_GATTC_NOTIFY_EVT:
            ol305_recive_message(p_data->notify.value, p_data->notify.value_len);
            break;

        case ESP_GATTC_WRITE_DESCR_EVT:
            if (p_data->write.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "write descr failed, error status = %x", p_data->write.status);
                break;
            }
            ESP_LOGI(TAG, "write descr success"); 
            ble_connection = true;
            break;

        case ESP_GATTC_SRVC_CHG_EVT:
            esp_bd_addr_t bda;
            memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
            esp_log_buffer_hex(TAG, bda, sizeof(esp_bd_addr_t));
            break;

        case ESP_GATTC_WRITE_CHAR_EVT:
            if (p_data->write.status != ESP_GATT_OK)
            {
                ESP_LOGE(TAG, "write char failed, error status = %x", p_data->write.status);
                break;
            }
            ESP_LOGD(TAG, "write char success");
            break;

        case ESP_GATTC_DISCONNECT_EVT:
            ble_connection = false;
            get_server = false;
            ESP_LOGI(TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
            
            uint32_t scan_duration = 30;
            esp_err_t scan_retry = esp_ble_gap_start_scanning(scan_duration);
            if (scan_retry != ESP_OK) 
            {
                ESP_LOGE(TAG, "Failed to start scanning for reconnection, error: %s", esp_err_to_name(scan_retry));
            }
            else 
            {
                ESP_LOGI(TAG, "Started scanning to reconnect...");
            }
            break;

        default:
            break;
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            uint32_t duration = 30;
            esp_ble_gap_start_scanning(duration);
            break;

        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
                break;
            }
            ESP_LOGI(TAG, "scan start success");
            break;

        case ESP_GAP_BLE_SCAN_RESULT_EVT:
        {
            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
            switch (scan_result->scan_rst.search_evt)
            {
                case ESP_GAP_SEARCH_INQ_RES_EVT:
                    if (memcmp(scan_result->scan_rst.bda, TARGET_MAC, sizeof(esp_bd_addr_t)) == 0)
                    {
                        ESP_LOGD(TAG, "connect to the remote device.");
                        esp_ble_gap_stop_scanning();
                        esp_ble_gattc_open(gl_profile_tab.gattc_if, scan_result->scan_rst.bda, scan_result->scan_rst.ble_addr_type, true);
                    }
                    break;

                case ESP_GAP_SEARCH_INQ_CMPL_EVT:
                    break;

                default:
                    break;
            }
            break;
        }

        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
                break;
            }
            ESP_LOGI(TAG, "stop scan successfully");
            break;

        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(TAG, "adv stop failed, error status = %x", param->adv_stop_cmpl.status);
                break;
            }
            ESP_LOGI(TAG, "stop adv successfully");
            break;

        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                    param->update_conn_params.status,
                    param->update_conn_params.min_int,
                    param->update_conn_params.max_int,
                    param->update_conn_params.conn_int,
                    param->update_conn_params.latency,
                    param->update_conn_params.timeout);
            break;

        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
            ESP_LOGI(TAG, "packet length updated: rx = %d, tx = %d, status = %d",
                    param->pkt_data_length_cmpl.params.rx_len,
                    param->pkt_data_length_cmpl.params.tx_len,
                    param->pkt_data_length_cmpl.status);
            break;

        default:
            break;
    }
}

static void esp_gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param)
{
    if (event == ESP_GATTC_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab.gattc_if = gattc_if;
        }
        else
        {
            ESP_LOGI(TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    if (gattc_if == ESP_GATT_IF_NONE || gattc_if == gl_profile_tab.gattc_if)
    {
        if (gl_profile_tab.gattc_cb)
        {
            gl_profile_tab.gattc_cb(event, gattc_if, param);
        }
    }
}

void ble_init()
{
    esp_err_t ret;
    if (true == firts_time)
    {    
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
        firts_time = false;
    }

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gap register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gattc_register_callback(esp_gattc_cb);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gattc register failed, error code = %x", __func__, ret);
        return;
    }

    ret = esp_ble_gattc_app_register(INVALID_HANDLE);
    if (ret)
    {
        ESP_LOGE(TAG, "%s gattc app register failed, error code = %x", __func__, ret);
    }

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret)
    {
        ESP_LOGE(TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
}

void ble_write(uint8_t *data, uint16_t len)
{
    esp_ble_gattc_write_char( gl_profile_tab.gattc_if,
                            gl_profile_tab.conn_id,
                            gl_profile_tab.write_handle,
                            len,
                            data,
                            ESP_GATT_WRITE_TYPE_RSP,
                            ESP_GATT_AUTH_REQ_NONE);
}

void set_target_mac(uint8_t* new_mac_addr,uint16_t mac_len)
{
    
    if (mac_len != sizeof(TARGET_MAC)) 
    {
        ESP_LOGE(TAG, "Error: Invalid MAC address length. Expected %d, but got %04x", sizeof(TARGET_MAC), mac_len);
        return;
    }

    if (is_ble_connected() == true)
    {
        esp_ble_gattc_close(gl_profile_tab.gattc_if, gl_profile_tab.conn_id);
        ESP_LOGI(TAG, "BLE connection disconnected.");
    }

    memcpy(TARGET_MAC, new_mac_addr, sizeof(TARGET_MAC));
    ESP_LOGI(TAG, "Search MAC : %02x:%02x:%02x:%02x:%02x:%02x",
             TARGET_MAC[0], TARGET_MAC[1], TARGET_MAC[2],
             TARGET_MAC[3], TARGET_MAC[4], TARGET_MAC[5]);
}

void set_uuid(uint8_t *uuid, uuid_type type)
{
    switch (type)
    {
        case SERVICE_UUID:
            remote_filter_service_uuid.len= ESP_UUID_LEN_128;
            memcpy(remote_filter_service_uuid.uuid.uuid128,uuid,remote_filter_service_uuid.len);
            break;
        
        case WRITE_UUID:
            remote_filter_char_uuid.len= ESP_UUID_LEN_128;
            memcpy(remote_filter_char_uuid.uuid.uuid128,uuid,remote_filter_char_uuid.len);
            break;
        
        case NOTIFY_UUID:
            notify_uuid.len= ESP_UUID_LEN_128;
            memcpy(notify_uuid.uuid.uuid128,uuid,notify_uuid.len);
            break;

        case NOTIFY_DESCR_UUID:
            notify_decr_uuid.len= ESP_UUID_LEN_16;
            uint16_t descr_uuid = (uint16_t)((uuid[0] << 8) + (uuid[1] & 0x00ff));
            notify_decr_uuid.uuid.uuid16 = descr_uuid;
            break;

        default:
            break;
    }
}