//#############################################################################
//
// FILE:   led_hw.c
//
// TITLE:  LED hardware adapter (driverlib GPIO).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "led_hw.h"
#include "board_pins.h"
#include "driverlib.h"
#include "device.h"

//! Channel index -> GPIO number and pin mux configuration.
//! Index 0 = LED4 (red, GPIO20), index 1 = LED5 (green, GPIO22).
static const struct
{
    uint32_t pin;
    uint32_t pin_config;
} led_hw_channels[LEDCHASER_CHANNEL_COUNT] = {
    { BOARD_LED4_GPIO, BOARD_LED4_PINCFG },
    { BOARD_LED5_GPIO, BOARD_LED5_PINCFG }
};

void led_hw_init(void)
{
    uint32_t i;

    for (i = 0U; i < LEDCHASER_CHANNEL_COUNT; i++)
    {
        uint32_t pin = led_hw_channels[i].pin;

        GPIO_setPinConfig(led_hw_channels[i].pin_config);
        GPIO_setDirectionMode(pin, GPIO_DIR_MODE_OUT);
        GPIO_setPadConfig(pin, GPIO_PIN_TYPE_STD);

        //
        // Active-low board: writing the inactive level (1) turns the LED off.
        //
        GPIO_writePin(pin, BOARD_LED_ACTIVE_LOW ? 1U : 0U);
    }
}

void led_hw_apply(uint32_t levels)
{
    uint32_t i;

    for (i = 0U; i < LEDCHASER_CHANNEL_COUNT; i++)
    {
        GPIO_writePin(led_hw_channels[i].pin, (levels >> i) & 0x1U);
    }
}
