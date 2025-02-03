#include "nvs.h"
#include "nvs_flash.h"
#include "ol305.h"
#include "test_ol305.h"
#include "freertos/FreeRTOS.h"

const static char *password  = "yOTmK50z";

void app_main(void)
{
    nvs_flash_init();
    set_ol305_ble_password(password);
    //the specific mac address of the OL305
    uint8_t mac_addr[] = {0xd5, 0x7b, 0xf1, 0xca, 0x51, 0x51};
    set_ol305_mac_addr(mac_addr,sizeof(mac_addr));
    xTaskCreate(&ol305_task,"OL305_TASK", 5000, NULL, 5 , NULL);
    xTaskCreate(&test_task,"TEST_TASK", 5000, NULL, 5 , NULL);
}