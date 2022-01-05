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

static const char *TAG = "HA_USB_SWITCH";

#define DEFAULT_VREF                    1100
#define N_SAMPLES                       128

#define WIFI_CONNECTED_BIT              BIT0
#define WIFI_FAIL_BIT                   BIT1

#define SWITCH_BUTTON_GPIO              13

#define OUTPUT_A                        0
#define OUTPUT_B                        1
#define OUTPUT_INVALID                  2

#define OUTPUT_A_NAME                   "Desktop"
#define OUTPUT_B_NAME                   "Laptop"

#define MQTT_COMMAND_TOPIC_SUFFX        "command"
#define MQTT_STATE_TOPIC_SUFFX          "state"

static esp_adc_cal_characteristics_t    *sg_adc_chars;
static const adc_atten_t                sg_atten = ADC_ATTEN_DB_11;
static const adc_unit_t                 sg_unit = ADC_UNIT_1;
static const adc_bits_width_t           sg_width = ADC_WIDTH_BIT_12;
static const adc_channel_t              sg_channel_input_a = ADC_CHANNEL_6; // GPIO34
static const adc_channel_t              sg_channel_input_b = ADC_CHANNEL_7; // GPIO35

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

// static void switch_output(void)
// {

//     gpio_set_level(13, 0);
//     vTaskDelay(50 / portTICK_PERIOD_MS);
//     gpio_set_level(13, 1);
// }

// static int get_active_output(void)
// {
//     uint32_t    a_reading = 0;
//     uint32_t    b_reading = 0;
//     uint32_t    a_voltage, b_voltage = 0;

//     for (int i = 0; i < N_SAMPLES; i++)
//     {
//         a_reading += adc1_get_raw((adc1_channel_t) sg_channel_input_a);
//         b_reading += adc1_get_raw((adc1_channel_t) sg_channel_input_b); 

//     }

//     a_reading /= N_SAMPLES;
//     b_reading /= N_SAMPLES;

//     a_voltage = esp_adc_cal_raw_to_voltage(a_reading, sg_adc_chars);
//     b_voltage = esp_adc_cal_raw_to_voltage(b_reading, sg_adc_chars);

//     ESP_LOGI(TAG, "Input A = %d | %d", a_voltage, a_reading);
//     ESP_LOGI(TAG, "Input B = %d | %d", b_voltage, b_reading);

//     if (a_voltage < b_voltage)
//     {
//         return OUTPUT_B;
//     }
//     else
//     {
//         return OUTPUT_A;
//     }
// }

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t    sg_client = event->client;
    int msg_id;
    
    char topic_buffer[256] = {0};
    int i, active_output, temp_output;

    char *payload_in, *payload_out;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

            // publish online state
            msg_id = esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" CONFIG_MQTT_AVAILABLE_TOPIC_SUFFX,
                                                                    CONFIG_MQTT_AVAILABLE_ONLINE_PAYLOAD, 0, 1, 1);
            ESP_LOGI(TAG, "set status to online successful, msg_id=%d", msg_id);

            active_output = usb_switch_get_active_output();
            if (active_output == OUTPUT_B)
            {
                payload_out = OUTPUT_B_NAME;
            }
            else
            {
                payload_out = OUTPUT_A_NAME;
            }


            msg_id = esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" MQTT_STATE_TOPIC_SUFFX,
                                                                    payload_out, 0, 1, 1);
            ESP_LOGI(TAG, "pub state, msg_id=%d", msg_id);


            // publish initial device state

            // subscribe to the command topic
            // snprintf(topic_buffer, 256, "%s/%s", CONFIG_MQTT_BASE_TOPIC, MQTT_COMMAND_TOPIC_SUFFX);

            // // subscribe to all of the command topics
            // for (i = 0; i < sg_n_capabilities; i++)
            // {
            //     ESP_LOGI(TAG, "%s", sg_capabilities[i]);
            //     snprintf(topic_buffer, 256, "%s/%s/%s", CONFIG_MQTT_BASE_TOPIC, sg_capabilities[i], DDC_MQTT_COMMAND_TOPIC_SUFFX);

            msg_id = esp_mqtt_client_subscribe(sg_client, CONFIG_MQTT_BASE_TOPIC "/" MQTT_COMMAND_TOPIC_SUFFX, 0);
            //     ESP_LOGI(TAG, "subscribe to \"%s\" successful, msg_id=%d", topic_buffer, msg_id);
            // }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            
            payload_in = (char *) calloc(event->data_len + 1, sizeof(char));
            memcpy((void *) payload_in, (void *) event->data, event->data_len);
            
            // capability_event_state = process_event_data(event);
            
            ESP_LOGI(TAG, "Got new command = %s", payload_in);

            if (strcmp(payload_in, "Desktop") == 0)
            {
                active_output = OUTPUT_A;
            }
            else if (strcmp(payload_in, "Laptop") == 0)
            {
                active_output = OUTPUT_B;
            }
            else
            {
                active_output = OUTPUT_INVALID;
            }

            if (active_output != OUTPUT_INVALID)
            {
                if (active_output != usb_switch_get_active_output())
                {
        
                    xSemaphoreTake(sg_output_lock, portMAX_DELAY);
                    usb_switch_change_toggle_output();

                    vTaskDelay(20 / portTICK_PERIOD_MS);
                    sg_curr_output = active_output;

                    active_output = usb_switch_get_active_output();

                    xSemaphoreGive(sg_output_lock);

                }

                if (active_output == OUTPUT_B)
                {
                    payload_out = OUTPUT_B_NAME;
                }
                else
                {
                    payload_out = OUTPUT_A_NAME;
                }

                // xSemaphoreTake(sg_output_lock, portMAX_DELAY);
                // sg_curr_output = active_output;
                // xSemaphoreGive(sg_output_lock);

                msg_id = esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" MQTT_STATE_TOPIC_SUFFX,
                                                                        payload_out, 0, 1, 1);
                ESP_LOGI(TAG, "pub state, msg_id=%d", msg_id);


            }

            free(payload_in);

            // ESP_LOGI(TAG, "cmdq ptr = %p", sg_shared_params->command_queue);
            // xQueueSend(sg_shared_params->command_queue, (void *) &capability_event_state, (TickType_t)(10));

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static esp_mqtt_client_config_t mqtt_get_default_config(void)
{
    esp_mqtt_client_config_t client_cfg = {
        .host = CONFIG_MQTT_HOST,
        .port = CONFIG_MQTT_PORT,
        .username = CONFIG_MQTT_USERNAME,
        .password = CONFIG_MQTT_PASSWORD,
        .lwt_topic = CONFIG_MQTT_BASE_TOPIC "/" CONFIG_MQTT_AVAILABLE_TOPIC_SUFFX,
        .lwt_msg = CONFIG_MQTT_AVAILABLE_OFFLINE_PAYLOAD,
        .lwt_msg_len = strlen(CONFIG_MQTT_AVAILABLE_OFFLINE_PAYLOAD),
    };

    return client_cfg;
}

void mqtt_init(void)
{
    esp_mqtt_client_config_t client_cfg = mqtt_get_default_config();
    sg_client = esp_mqtt_client_init(&client_cfg);
    esp_mqtt_client_register_event(sg_client, ESP_EVENT_ANY_ID, mqtt_event_handler, sg_client);
    
    esp_mqtt_client_start(sg_client);

    ESP_LOGI(TAG, "mqtt connect!");
    return;
}

static void check_efuse(void)
{
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        printf("eFuse Two Point: Supported\n");
    } else {
        printf("eFuse Two Point: NOT supported\n");
    }

    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        printf("eFuse Vref: Supported\n");
    } else {
        printf("eFuse Vref: NOT supported\n");
    }
}

static esp_err_t adc_init(void)
{
    check_efuse();

    adc1_config_width(sg_width);
    adc1_config_channel_atten(sg_channel_input_a, sg_atten);
    adc1_config_channel_atten(sg_channel_input_b, sg_atten);

    sg_adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));

    esp_adc_cal_characterize(sg_unit, sg_atten, sg_width, DEFAULT_VREF, sg_adc_chars);

    return ESP_OK;
}


void app_main(void)
{
    int i;
    int active_output;
    ESP_LOGI(TAG, "switch start!");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "NVS init OK!");

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Default Event Loop Creation OK!");

    adc_init();

    // turn on status led
    gpio_reset_pin(15);
    gpio_set_direction(15, GPIO_MODE_OUTPUT);
    gpio_set_level(15, 0);

    // // setup pin, set to high initially so the physical button will work
    // gpio_reset_pin(SWITCH_BUTTON_GPIO);
    // gpio_set_direction(SWITCH_BUTTON_GPIO, GPIO_MODE_INPUT_OUTPUT);
    // gpio_set_level(SWITCH_BUTTON_GPIO, 1);

    sg_output_lock = xSemaphoreCreateMutex();

    // // set up adc
    // adc_init();
    usb_switch_init();

    wifi_init();    // initialize wifi using configured credentials

    mqtt_init();

    char *output_name = NULL;

    sg_curr_output = usb_switch_get_active_output();

    int msg_id, temp_output;

    while(1)
    {
        active_output = usb_switch_get_active_output();
        switch(active_output)
        {
            case OUTPUT_A:
                output_name = "Desktop";
                break;
            case OUTPUT_B:
                output_name = "Laptop";
                break;
            default:
                break;
        }

        xSemaphoreTake(sg_output_lock, portMAX_DELAY);
        temp_output = sg_curr_output;
        xSemaphoreGive(sg_output_lock);

        if (active_output != temp_output)
        {
            msg_id = esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" MQTT_STATE_TOPIC_SUFFX,
                                                                    output_name, 0, 1, 1);
            ESP_LOGI(TAG, "pub state, msg_id=%d", msg_id);
        }

        xSemaphoreTake(sg_output_lock, portMAX_DELAY);
        sg_curr_output = active_output;
        xSemaphoreGive(sg_output_lock);


        // ESP_LOGI(TAG, "Currently connected to %s", output_name);
        

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}