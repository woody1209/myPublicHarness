//#############################################################################
//
// FILE:   self_test.c
//
// TITLE:  Power-on self test (pure logic - runs on target AND on host).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "self_test.h"
#include "led_pattern.h"
#include "fmt.h"

volatile uint32_t g_self_test_code = (uint32_t)SELF_TEST_OK;

//! Expected CHASE masks for the reference channel count (first 8 steps).
static const uint32_t self_test_expected_chase[8] = {
    0x1U, 0x2U, 0x1U, 0x2U, 0x1U, 0x2U, 0x1U, 0x2U
};

//! Expected BLINK masks for the reference channel count (first 8 steps).
static const uint32_t self_test_expected_blink[8] = {
    0x3U, 0x0U, 0x3U, 0x0U, 0x3U, 0x0U, 0x3U, 0x0U
};

//! strcmp()-free comparison so this module stays free of libc dependencies.
static bool self_test_str_eq(const char *a, const char *b)
{
    uint32_t i = 0U;

    if ((a == 0) || (b == 0))
    {
        return false;
    }

    while ((a[i] != '\0') && (b[i] != '\0'))
    {
        if (a[i] != b[i])
        {
            return false;
        }
        i++;
    }

    return (a[i] == b[i]);
}

self_test_result_t self_test_run(void)
{
    char buf[24];
    uint32_t step;
    int32_t written;
    self_test_result_t result = SELF_TEST_OK;

    //
    // 1) FNV-1a over the first 8 steps of every mode.
    //
    if (led_pattern_reference_fnv1a((uint16_t)LED_PATTERN_REF_CHANNELS) !=
        LED_PATTERN_REF_FNV1A)
    {
        result = SELF_TEST_FAIL_PATTERN_FNV;
    }

    //
    // 2) Hard-coded per-step sequences (independent of the reference checksum,
    //    so a broken reference cannot mask a broken implementation).
    //
    if (result == SELF_TEST_OK)
    {
        for (step = 0U; step < 8U; step++)
        {
            if (led_pattern_mask(LED_PATTERN_MODE_CHASE,
                                 (uint16_t)LED_PATTERN_REF_CHANNELS, step) !=
                self_test_expected_chase[step])
            {
                result = SELF_TEST_FAIL_PATTERN_SEQ;
                break;
            }
            if (led_pattern_mask(LED_PATTERN_MODE_BLINK,
                                 (uint16_t)LED_PATTERN_REF_CHANNELS, step) !=
                self_test_expected_blink[step])
            {
                result = SELF_TEST_FAIL_PATTERN_SEQ;
                break;
            }
        }
    }

    //
    // 3) Boundary of the pattern engine: an invalid channel count must yield 0.
    //
    if (result == SELF_TEST_OK)
    {
        if ((led_pattern_mask(LED_PATTERN_MODE_CHASE, 0U, 0U) != 0U) ||
            (led_pattern_mask(LED_PATTERN_MODE_CHASE,
                              LED_PATTERN_MAX_CHANNELS + 1U, 0U) != 0U))
        {
            result = SELF_TEST_FAIL_PATTERN_SEQ;
        }
    }

    //
    // 4) Formatting reference vectors.
    //
    if (result == SELF_TEST_OK)
    {
        written = fmt_u32_dec(0U, buf, sizeof(buf));
        if ((written != 1) || !self_test_str_eq(buf, "0"))
        {
            result = SELF_TEST_FAIL_FMT_DEC;
        }
    }
    if (result == SELF_TEST_OK)
    {
        written = fmt_u32_dec(4294967295U, buf, sizeof(buf));
        if ((written != 10) || !self_test_str_eq(buf, "4294967295"))
        {
            result = SELF_TEST_FAIL_FMT_DEC;
        }
    }
    if (result == SELF_TEST_OK)
    {
        written = fmt_u32_hex(0x1A2BU, buf, sizeof(buf));
        if ((written != 6) || !self_test_str_eq(buf, "0x1A2B"))
        {
            result = SELF_TEST_FAIL_FMT_HEX;
        }
    }

    //
    // 5) Buffer bound handling: a 4-byte buffer cannot hold "1000" + NUL.
    //
    if (result == SELF_TEST_OK)
    {
        buf[0] = 'X';
        if (fmt_u32_dec(1000U, buf, 4U) != FMT_ERR_TOO_SMALL)
        {
            result = SELF_TEST_FAIL_FMT_BOUND;
        }
    }

    g_self_test_code = (uint32_t)result;
    return result;
}
