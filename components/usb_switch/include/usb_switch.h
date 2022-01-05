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

#define USB_SWITCH_N_SAMPLES            128                 // just get a bunch of samples

#define USB_SWITCH_ADC_ATTEN            ADC_ATTEN_DB_11     // we need full resolution
#define USB_SWITCH_ADC_UNIT             ADC_UNIT_1          // using ADC 1
#define USB_SWITCH_ADC_WIDTH            ADC_WIDTH_BIT_12

#define USB_SWITCH_BUTTON_GPIO_PIN      13
#define USB_SWITCH_OUTPUT_A_CHANNEL     ADC_CHANNEL_6       // channel for getting output A driver voltage | GPIO34 
#define USB_SWITCH_OUTPUT_B_CHANNEL     ADC_CHANNEL_7       // channel for getting output B driver voltage | GPIO35

#define USB_SWITCH_OUTPUT_A_NAME        CONFIG_USB_SWITCH_OUTPUT_A_NAME
#define USB_SWITCH_OUTPUT_B_NAME        CONFIG_USB_SWITCH_OUTPUT_B_NAME

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