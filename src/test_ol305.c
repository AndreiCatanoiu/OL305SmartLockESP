#include <string.h>
#include "ble_connection.h"
#include "ol305.h"
#include "esp_timer.h"

void test_task()
{
    char input;
    while (1)
    {
        input = getchar();
        if (!is_ol305_connected())
        {
            vTaskDelay (250 / portTICK_PERIOD_MS);
            continue;
        }

        switch (input)
        {
            case '1':
                ol305_unlock();
                break;

            case '2':
                ol305_query();
                break;
                
            case '3':
                ol305_read_rfid();
                break;

            case '4':
                ol305_delete_rfid();
                break;
                
            case '5':
                ol305_settings();
                break;

            case '6':
                ol305_disconnect();
                break;
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}