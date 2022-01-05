/**
 * @file main.c
 * 
 * @brief a simple wifi addon to make your usb switch smart
 * 
 * @author byt3swap
 */

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include "freertos/event_groups.h"
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include "usb_switch.h"
#include "usb_switch_mqtt.h"
#include "usb_switch_wifi.h"

static const char *TAG = "MAIN";

/**
 * @brief callback that changes the input when commands are receieved
 * 
 * @return ESP_OK if success, error otherwise
 */
static esp_err_t output_set_callback(usb_switch_output_t output)
{
    esp_err_t err = ESP_OK;

    if (output != usb_switch_get_active_output())
    {
        err = usb_switch_toggle_output();
    }

    return err;
}

void app_main(void)
{
    usb_switch_output_t last_active, curr_active;

    ESP_LOGI(TAG, "switch start!");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "NVS init OK!");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Default Event Loop Creation OK!");

    // turn on status led
    gpio_reset_pin(15);
    gpio_set_direction(15, GPIO_MODE_OUTPUT);
    gpio_set_level(15, 0);

    // set up the important stuff
    usb_switch_init();
    usb_switch_wifi_init();    
    usb_switch_mqtt_init(output_set_callback);

    last_active = usb_switch_get_active_output();

    while(1)
    {
        curr_active = usb_switch_get_active_output();
        if (curr_active != last_active)
        {
            usb_switch_mqtt_pub_state(curr_active);
        }

        last_active = curr_active;

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}