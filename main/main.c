#include "sdkconfig.h"

#include <freertos/FreeRTOS.h>
#include "freertos/event_groups.h"
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_spi_flash.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <mqtt_client.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "usb_switch.h"
#include "usb_switch_mqtt.h"

static const char *TAG = "MAIN";

#define WIFI_CONNECTED_BIT              BIT0
#define WIFI_FAIL_BIT                   BIT1

static EventGroupHandle_t               sg_wifi_event_group;
static int                              sg_wifi_retries = 0;

static esp_mqtt_client_handle_t         sg_client;

static int                              sg_curr_output;
SemaphoreHandle_t                       sg_output_lock;

/**
 * @brief wifi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(sg_wifi_event_group, WIFI_CONNECTED_BIT);
        if (sg_wifi_retries < CONFIG_WIFI_MAX_RECONNECT)
        {
            esp_wifi_connect();
            sg_wifi_retries++;
            ESP_LOGI(TAG, "reconnect attempt %d...", sg_wifi_retries - 1);
        }
        else
        {
            xEventGroupSetBits(sg_wifi_event_group, WIFI_FAIL_BIT);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        sg_wifi_retries = 0;
        xEventGroupSetBits(sg_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    
}

static void wifi_init(void)
{
    sg_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(sg_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (!(bits & WIFI_CONNECTED_BIT))
    {
        ESP_LOGE(TAG, "WiFi Config Fail, restarting!");
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        esp_restart();
    }
}

static esp_err_t output_set_callback(usb_switch_output_t output)
{
    esp_err_t err = ESP_OK;

    ESP_LOGE(TAG, "got = %s", usb_switch_get_output_name(output));

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

    sg_output_lock = xSemaphoreCreateMutex();

    // // set up adc
    // adc_init();
    usb_switch_init();

    wifi_init();    // initialize wifi using configured credentials

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