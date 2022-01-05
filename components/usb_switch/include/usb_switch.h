/**
 * @file usb_switch.h
 * 
 * Library for interacting with a modified Pluggable USB 3.0 switch.
 * 
 * Reqires soldering wires to the pins driving the status LEDs for reading status,
 * and soldering an additional wire the the button input pin to support changing
 * outputs.
 * 
 * @author byt3swap
 */

#ifndef USB_SWITCH_H_
#define USB_SWITCH_H_

#include <esp_err.h>

typedef enum {
    USB_SWITCH_OUTPUT_A,
    USB_SWITCH_OUTPUT_B,
    USB_SWITCH_OUTPUT_INVALID
} usb_switch_output_t;

char *usb_switch_get_output_name(usb_switch_output_t output);
usb_switch_output_t usb_switch_get_active_output(void);
esp_err_t usb_switch_change_toggle_output(void);
esp_err_t usb_switch_init(void);

#endif // USB_SWITCH_H_