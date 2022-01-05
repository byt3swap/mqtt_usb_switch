/**
 * @file usb_switch.c
 * 
 * Library for interacting with a modified Pluggable USB 3.0 switch.
 * 
 * Reqires soldering wires to the pins driving the status LEDs for reading status,
 * and soldering an additional wire the the button input pin to support changing
 * outputs.
 * 
 * @author byt3swap
 */

#include <sdkconfig.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "usb_switch.h"

// #define USB_SWITCH_ADC_VREF             1100                // standard default VREF for ADC
#define USB_SWITCH_N_SAMPLES            128                 // just get a bunch of samples

#define USB_SWITCH_ADC_ATTEN            ADC_ATTEN_DB_11     // we need full resolution
#define USB_SWITCH_ADC_UNIT             ADC_UNIT_1          // using ADC 1
#define USB_SWITCH_ADC_WIDTH            ADC_WIDTH_BIT_12

#define USB_SWITCH_BUTTON_GPIO_PIN      13
#define USB_SWITCH_OUTPUT_A_CHANNEL     ADC_CHANNEL_6       // channel for getting output A driver voltage | GPIO34 
#define USB_SWITCH_OUTPUT_B_CHANNEL     ADC_CHANNEL_7       // channel for getting output B driver voltage | GPIO35

#define USB_SWITCH_OUTPUT_A_NAME        CONFIG_USB_SWITCH_OUTPUT_A_NAME
#define USB_SWITCH_OUTPUT_B_NAME        CONFIG_USB_SWITCH_OUTPUT_B_NAME

static const char                       *TAG = "USB_SWITCH";

static esp_adc_cal_characteristics_t    *sgp_adc_chars;


/**
 * @brief get the name of the output
 * 
 * @param[in] output the output to get the name for
 * 
 * @return string representation of the output name
 */
char *usb_switch_get_output_name(usb_switch_output_t output)
{
    switch(output)
    {
        case USB_SWITCH_OUTPUT_A:
            return USB_SWITCH_OUTPUT_A_NAME;
        case USB_SWITCH_OUTPUT_B:
            return USB_SWITCH_OUTPUT_B_NAME;
        default:
            return NULL;
    }
}

/**
 * @brief get the current output
 * 
 * Due to the way the LEDs are driven, the status output pins
 * running to the LEDs are always about the ~0.75v threshold
 * for reading a high voltage. Due to this, we do an analog
 * read on each channel, and return index of the channel with
 * the higher value.
 * 
 * @return the currently active output
 */
usb_switch_output_t usb_switch_get_active_output(void)
{
    uint32_t        a_raw = 0;
    uint32_t        b_raw = 0;

    for (int i = 0; i < USB_SWITCH_N_SAMPLES; i++)
    {
        a_raw += adc1_get_raw((adc1_channel_t) USB_SWITCH_OUTPUT_A_CHANNEL);
        b_raw += adc1_get_raw((adc1_channel_t) USB_SWITCH_OUTPUT_B_CHANNEL);
    }

    a_raw /= USB_SWITCH_N_SAMPLES;
    b_raw /= USB_SWITCH_N_SAMPLES;

    return (a_raw > b_raw ? USB_SWITCH_OUTPUT_A : USB_SWITCH_OUTPUT_B);
}

/**
 * @brief toggle the output
 * 
 * simulated pressing the button on the switch
 */
esp_err_t usb_switch_change_toggle_output(void)
{
    esp_err_t   err;

    err = gpio_set_level(USB_SWITCH_BUTTON_GPIO_PIN, 0);
    if (err == ESP_OK)
    {
        vTaskDelay(50 / portTICK_PERIOD_MS);    // low for 50ms
    
        err = gpio_set_level(USB_SWITCH_BUTTON_GPIO_PIN, 1);
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to toggle output! - %d", err);
    }

    return err;
}

/**
 * @brief initialize usb switch interaction
 * 
 * Sets up a GPIO pin for simulating button press,
 * and sets up analog inputs (yeah I know) for checking
 * the current output.
 * 
 * @return ESP_OK on success, error on failure
 */
esp_err_t usb_switch_init(void)
{
    esp_err_t   err;

    // Button GPIO setup
    err = gpio_reset_pin(USB_SWITCH_BUTTON_GPIO_PIN);
    if (err == ESP_OK)
    {
        err = gpio_set_direction(USB_SWITCH_BUTTON_GPIO_PIN, GPIO_MODE_INPUT_OUTPUT);
        if (err == ESP_OK)
        {
            err = gpio_set_level(USB_SWITCH_BUTTON_GPIO_PIN, 1); // button is high by default
        }
    }

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "usb switch button config success!");
    }
    else
    {
        ESP_LOGE(TAG, "usb switch button config failure! - %d", err);
        return err;
    }

    // ADC setup
    err = adc1_config_width(USB_SWITCH_ADC_WIDTH);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "adc1 config width fail! - %d", err);
        return err;
    }

    err = adc1_config_channel_atten(USB_SWITCH_OUTPUT_A_CHANNEL, USB_SWITCH_ADC_ATTEN);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "attenuation configuration for output A fail! - %d", err);
        return err;
    }

    err = adc1_config_channel_atten(USB_SWITCH_OUTPUT_B_CHANNEL, USB_SWITCH_ADC_ATTEN);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "attenuation configuration for output B fail! - %d", err);
        return err;
    }

    return err;
}
