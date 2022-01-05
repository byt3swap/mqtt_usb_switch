/**
 * @file usb_switch_wifi.h
 * 
 * @brief breakout of wifi handling to seperate component to keep main clean
 * 
 * @author byt3swap
 */

#ifndef USB_SWITCH_WIFI_H_
#define USB_SWITCH_WIFI_H_

#include <esp_system.h>

#define USB_SWITCH_WIFI_CONNECTED_BIT       BIT0
#define USB_SWITCH_WIFI_FAIL_BIT            BIT1

esp_err_t usb_switch_wifi_init(void);

#endif // USB_SWITCH_WIFI_H_