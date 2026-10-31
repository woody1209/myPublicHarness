//#############################################################################
//
// FILE:   led_pattern.c
//
// TITLE:  LED chaser pattern engine (pure logic, hardware independent).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "led_pattern.h"

//*****************************************************************************
//
// Helpers
//
//*****************************************************************************

//! Mask of all valid channels (returns 0 for an invalid channel count).
static uint32_t led_pattern_channel_fill(uint16_t channel_count)
{
    if ((channel_count == 0U) || (channel_count > LED_PATTERN_MAX_CHANNELS))
    {
        return 0U;
    }
    if (channel_count == LED_PATTERN_MAX_CHANNELS)
    {
        return 0xFFFFFFFFU;
    }
    return (1UL << channel_count) - 1UL;
}

//*****************************************************************************
//
// Public API
//
//*****************************************************************************

uint32_t led_pattern_mask(led_pattern_mode_t mode, uint16_t channel_count,
                          uint32_t step)
{
    uint32_t fill;
    uint32_t period;
    uint32_t t;
    uint32_t idx;

    fill = led_pattern_channel_fill(channel_count);
    if (fill == 0U)
    {
        return 0U;                     // invalid channel count -> no LED lit, no UB
    }

    switch (mode)
    {
        case LED_PATTERN_MODE_CHASE:
            idx = step % channel_count;
            return (1UL << idx);

        case LED_PATTERN_MODE_BOUNCE:
            period = (channel_count > 1U) ? ((2UL * channel_count) - 2UL) : 1UL;
            t = step % period;
            idx = (t < channel_count) ? t : (period - t);
            return (1UL << idx);

        case LED_PATTERN_MODE_BLINK:
            return ((step & 1UL) == 0UL) ? fill : 0U;

        default:
            return 0U;                 // unknown mode -> all dark
    }
}

uint32_t led_pattern_levels(uint32_t mask, uint16_t channel_count,
                            bool active_low)
{
    uint32_t fill = led_pattern_channel_fill(channel_count);

    if (fill == 0U)
    {
        return 0U;
    }

    mask &= fill;

    return active_low ? (~mask & fill) : mask;
}

uint32_t led_pattern_reference_fnv1a(uint16_t channel_count)
{
    const uint32_t fnv_prime = 16777619UL;
    uint32_t hash = 2166136261UL;      // FNV-1a 32-bit offset basis
    uint32_t mode;
    uint32_t step;
    uint32_t mask;
    uint32_t i;

    for (mode = 0U; mode < (uint32_t)LED_PATTERN_MODE_COUNT; mode++)
    {
        for (step = 0U; step < 8U; step++)
        {
            mask = led_pattern_mask((led_pattern_mode_t)mode, channel_count, step);
            for (i = 0U; i < 4U; i++)   // mix in the mask, little endian byte order
            {
                hash ^= ((mask >> (8U * i)) & 0xFFU);
                hash *= fnv_prime;
            }
        }
    }

    return hash;
}
