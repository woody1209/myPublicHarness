//#############################################################################
//
// FILE:   led_pattern.h
//
// TITLE:  LED chaser pattern engine (pure logic, hardware independent).
//
// DESCRIPTION:
//   Computes the "logically lit" bit mask for a chaser/流水灯 given a mode, a
//   channel count and a step index, and maps such a mask to physical GPIO
//   levels honouring the polarity of the board (active-low on LAUNCHXL-F280039C).
//
//   This module is PURE LOGIC: it only depends on <stdint.h>/<stdbool.h>, so it
//   compiles for the host (unit tests) as well as for the C28x target. It holds
//   no mutable state - every result is derived from its arguments, which makes it
//   reentrant and exhaustively testable.
//
//   The reference constant LED_PATTERN_REF_FNV1A is consumed by BOTH the host
//   unit tests and the on-target power-on self test (see AGENTS.md: "no two
//   truths"). Change it deliberately and the host tests fail first.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef LEDCHASER_LED_PATTERN_H
#define LEDCHASER_LED_PATTERN_H

#include <stdint.h>
#include <stdbool.h>

//! Maximum number of channels supported by the engine (mask is uint32_t).
#define LED_PATTERN_MAX_CHANNELS          32U

//! FNV-1a (32-bit) checksum over the first 8 steps of all three modes with
//! LED_PATTERN_REF_CHANNELS channels. Shared by host tests and target self test.
#define LED_PATTERN_REF_CHANNELS          2U
#define LED_PATTERN_REF_FNV1A            0x436FEC05U

//! Chaser modes.
typedef enum
{
    LED_PATTERN_MODE_CHASE  = 0U,   //!< single lit channel moving in one direction, wraps around
    LED_PATTERN_MODE_BOUNCE = 1U,   //!< single lit channel bouncing between the ends
    LED_PATTERN_MODE_BLINK  = 2U,   //!< all channels lit / all channels off, alternating
    LED_PATTERN_MODE_COUNT  = 3U    //!< sentinel - not a valid mode
} led_pattern_mode_t;

//! Return the "logically lit" mask for a given mode/channel count/step.
//! A set bit means "this channel is lit" (before polarity conversion).
//! \param mode          one of led_pattern_mode_t
//! \param channel_count number of channels (1..LED_PATTERN_MAX_CHANNELS)
//! \param step          step index (wraps internally, any uint32_t is legal)
//! \return the mask, or 0 for invalid input
uint32_t led_pattern_mask(led_pattern_mode_t mode, uint16_t channel_count,
                          uint32_t step);

//! Convert a "logically lit" mask into physical GPIO levels.
//! Active-low boards drive a low level to light an LED, so the mask is inverted.
//! Only the lowest channel_count bits are meaningful; unused bits read as 0.
uint32_t led_pattern_levels(uint32_t mask, uint16_t channel_count,
                            bool active_low);

//! FNV-1a over the first 8 steps of every mode - used by the target self test.
uint32_t led_pattern_reference_fnv1a(uint16_t channel_count);

#endif // LEDCHASER_LED_PATTERN_H
