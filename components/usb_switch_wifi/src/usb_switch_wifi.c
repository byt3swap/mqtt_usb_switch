/**
 * @file usb_switch_wifi.c
 * 
 * @brief breakout of wifi handling to seperate component to keep main clean
 * 
 * @author byt3swap
 */

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include "freertos/event_groups.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "usb_switch_wifi.h"

static const char               *TAG = "USB_SWITCH_WIFI";

static EventGroupHandle_t       sg_usb_switch_wifi_event_group;
static int                      sg_usb_switch_wifi_retries = 0;

/**
 * @brief wifi event handler, based on espressif example
 * 
 * @param[in] arg unused ctx
 * @param[in] event_base base type of event
 * @param[in] event_id specific type of event which occurred
 * @param[in] event_data details of the event
 */
static void usb_switch_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                                int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(sg_usb_switch_wifi_event_group, USB_SWITCH_WIFI_CONNECTED_BIT);
        if (sg_usb_switch_wifi_retries < CONFIG_WIFI_MAX_RECONNECT)
        {
            esp_wifi_connect();
            sg_usb_switch_wifi_retries++;
            ESP_LOGI(TAG, "reconnect attempt %d...", sg_usb_switch_wifi_retries - 1);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        sg_usb_switch_wifi_retries = 0;
        xEventGroupSetBits(sg_usb_switch_wifi_event_group, USB_SWITCH_WIFI_CONNECTED_BIT);
    }
    
}

/**
 * @brief initialize wifi
 * 
 * heavily influenced by espressif example
 */
esp_err_t usb_switch_wifi_init(void)
{
    esp_err_t                       err;
    EventBits_t                     bits;
    esp_event_handler_instance_t    instance_any_id;
    esp_event_handler_instance_t    instance_got_ip;

    // create the wifi event group
    sg_usb_switch_wifi_event_group = xEventGroupCreate();

    // initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    // basic init
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));                  // keep ESP_ERROR_CHECK, restart on any failures in wifi setup
    
    // register the required events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &usb_switch_wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &usb_switch_wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // our config based on our Menuconfig settings
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASS,
    	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    // finish configuring and start it all up
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    // wait for wifi to connect or fail
    bits = xEventGroupWaitBits(sg_usb_switch_wifi_event_group,
            USB_SWITCH_WIFI_CONNECTED_BIT | USB_SWITCH_WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    // restart if we failed 
    if (!(bits & USB_SWITCH_WIFI_CONNECTED_BIT))
    {
        ESP_LOGE(TAG, "WiFi Config Fail, restarting!");
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        esp_restart();
    }

    return ESP_OK;
}