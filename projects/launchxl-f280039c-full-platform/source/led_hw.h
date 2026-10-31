//#############################################################################
//
// FILE:   led_hw.h
//
// TITLE:  LED hardware adapter (driverlib GPIO).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef LEDCHASER_LED_HW_H
#define LEDCHASER_LED_HW_H

#include <stdint.h>

//! Configure the LED GPIOs as standard push-pull outputs, all LEDs off.
void led_hw_init(void);

//! Drive the LED pins from a physical level word (bit i = level for channel i).
//! Levels are produced by led_pattern_levels(), which already applied polarity.
void led_hw_apply(uint32_t levels);

#endif // LEDCHASER_LED_HW_H
