/**
 * @file usb_switch_mqtt.c
 * 
 * sets up basic mqtt session with a single callback
 * 
 * @author byt3swap
 */

#include <sdkconfig.h>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>
#include <mqtt_client.h>

#include "usb_switch_mqtt.h"

static const char                           *TAG = "USB_SWITCH_MQTT";
static esp_mqtt_client_handle_t             sg_client;
static usb_switch_mqtt_payload_cb_t         sg_payload_cb = NULL;
static bool                                 sg_connected = false;

static SemaphoreHandle_t                    sg_connected_lock;

/**
 * @brief main mqtt event handler
 * 
 * @param[in] event mqtt event
 */
static esp_err_t usb_switch_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_err_t                   err = ESP_OK;
    esp_mqtt_event_handle_t     event = (esp_mqtt_event_handle_t) event_data;
    char                        *payload_ptr;

    switch(event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "mqtt connected!");

            usb_switch_mqtt_set_connected_status(true);

            // publish the 'online' message
            if (esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" CONFIG_MQTT_AVAILABLE_TOPIC_SUFFX,
                                                                CONFIG_MQTT_AVAILABLE_ONLINE_PAYLOAD, 0, 1, 1) < 0)
            {
                ESP_LOGE(TAG, "publish online status fail!");
                err = ESP_FAIL;
            }
            
            // publish the current state
            err = usb_switch_mqtt_pub_state(usb_switch_get_active_output());

            // subscribe to the command topic
            if (esp_mqtt_client_subscribe(sg_client, CONFIG_MQTT_BASE_TOPIC "/" USB_SWITCH_MQTT_COMMAND_TOPIC_SUFFX, 0) < 0)
            {
                err = ESP_FAIL;
            }
            else
            {
                ESP_LOGI(TAG, "subscribe success!");
            }

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGE(TAG, "MQTT disconnected!");
            
            usb_switch_mqtt_set_connected_status(false);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "data receieved!");

            // make a copy of the payload
            payload_ptr = (char *) calloc(event->data_len + 1, sizeof(char));
            memcpy((void *) payload_ptr, (void *) event->data, event->data_len);

            // convert and send through the callback
            err = (sg_payload_cb) (usb_switch_name_to_output(payload_ptr));

            // blow it way
            free(payload_ptr);

            break;
        default:
            break;
    }

    return err;
}

/**
 * @brief get a default config based on the Menuconfig settings
 * 
 * @return your default client config
 */
static esp_mqtt_client_config_t usb_switch_mqtt_get_default_config(void)
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

/**
 * @brief set the connection flag
 * 
 * @param[in] connected the state of the connection
 */
void usb_switch_mqtt_set_connected_status(bool connected)
{
    xSemaphoreTake(sg_connected_lock, portMAX_DELAY);
    sg_connected = connected;
    xSemaphoreGive(sg_connected_lock);
}

/**
 * @brief get the current connection flag status
 * 
 * @return current connected status
 */
bool usb_switch_mqtt_is_connected(void)
{
    bool ret;

    xSemaphoreTake(sg_connected_lock, portMAX_DELAY);
    ret = sg_connected;
    xSemaphoreGive(sg_connected_lock);

    return ret;
}

/**
 * @brief publish the current state
 * 
 * @param[in] current_output current output state to publihs
 * 
 * @return ESP_OK if successful, error if not
 */
esp_err_t usb_switch_mqtt_pub_state(usb_switch_output_t current_output)
{
    esp_err_t   err = ESP_OK;
    char        *payload_ptr = usb_switch_get_output_name(current_output);

    if (payload_ptr != NULL)
    {
        if (esp_mqtt_client_publish(sg_client, CONFIG_MQTT_BASE_TOPIC "/" USB_SWITCH_MQTT_STATE_TOPIC_SUFFX,
                                                                                    payload_ptr, 0, 1, 1) < 0)
        {
            ESP_LOGE(TAG, "publish state failure!");
            err = ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "failed to publish state! invalid payload!");
        err = ESP_FAIL;
    }

    return err;
}

/**
 * @brief initialize and connect to mqtt
 * 
 * @param[in] paylaod_cb callback to set the output received in the paylaod
 * 
 * @return ESP_OK if successful, error if not
 */
esp_err_t usb_switch_mqtt_init(usb_switch_mqtt_payload_cb_t payload_cb)
{
    esp_err_t                   err = ESP_OK;
    esp_mqtt_client_config_t    client_cfg = usb_switch_mqtt_get_default_config();

    // set up the client
    sg_client = esp_mqtt_client_init(&client_cfg);
    esp_mqtt_client_register_event(sg_client, ESP_EVENT_ANY_ID, usb_switch_mqtt_event_handler, sg_client);

    err = esp_mqtt_client_start(sg_client);
    if (err == ESP_OK)
    {
        // register the callback
        sg_payload_cb = payload_cb;

        // create the lock
        sg_connected_lock = xSemaphoreCreateMutex();
    }
    else
    {
        ESP_LOGE(TAG, "failed to start MQTT client!");
    }

    return err;
}