/**
 * @file usb_switch_mqtt.h
 * 
 * sets up basic mqtt session with a single callback
 * 
 * @author byt3swap
 */

#ifndef USB_SWITCH_MQTT_H_
#define USB_SWITCH_MQTT_H_

#include <sdkconfig.h>

#include <esp_err.h>

#include "usb_switch.h"

#define USB_SWITCH_MQTT_STATE_TOPIC_SUFFX   "state"
#define USB_SWITCH_MQTT_COMMAND_TOPIC_SUFFX "command"

typedef esp_err_t(*usb_switch_mqtt_payload_cb_t)(usb_switch_output_t output);

void usb_switch_mqtt_set_connected_status(bool connected);
bool usb_switch_mqtt_is_connected(void);
esp_err_t usb_switch_mqtt_pub_state(usb_switch_output_t current_output);
esp_err_t usb_switch_mqtt_init(usb_switch_mqtt_payload_cb_t payload_cb);

#endif // USB_SWITCH_MQTT_H_