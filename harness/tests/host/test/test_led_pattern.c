//#############################################################################
//
// FILE:   test_led_pattern.c
//
// TITLE:  Host-side unit tests for the pure-logic chaser pattern engine.
//
//   Covers REQ-001 (sequences, wrap-around, bounce, boundaries) and
//   REQ-002 (active-low polarity mapping). The reference sequences are hard
//   coded here on purpose: they are INDEPENDENT of the FNV-1a reference value
//   used by the on-target self test, so a broken reference cannot mask a
//   broken implementation.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "led_pattern.h"

void led_pattern_tests(void)
{
    uint32_t step;

    fw_test_suite_begin("led_pattern");

    // ---------------------------------------------------------------- CHASE --
    fw_test_case_begin("CHASE n=2 alternates and wraps");
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 2U, 0U));
    FW_ASSERT_EQ_U32(0x2U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 2U, 1U));
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 2U, 2U));   // wrap
    FW_ASSERT_EQ_U32(0x2U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 2U, 11U));  // odd step far away

    fw_test_case_begin("CHASE n=4 walks 0..3 then wraps");
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 4U, 0U));
    FW_ASSERT_EQ_U32(0x2U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 4U, 1U));
    FW_ASSERT_EQ_U32(0x4U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 4U, 2U));
    FW_ASSERT_EQ_U32(0x8U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 4U, 3U));
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 4U, 4U));   // wrap

    fw_test_case_begin("CHASE n=1 stays on channel 0");
    for (step = 0U; step < 8U; step++)
    {
        FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 1U, step));
    }

    // --------------------------------------------------------------- BOUNCE --
    fw_test_case_begin("BOUNCE n=4 reverses at the ends (period 2n-2 = 6)");
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 0U));
    FW_ASSERT_EQ_U32(0x2U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 1U));
    FW_ASSERT_EQ_U32(0x4U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 2U));
    FW_ASSERT_EQ_U32(0x8U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 3U));
    FW_ASSERT_EQ_U32(0x4U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 4U));  // coming back
    FW_ASSERT_EQ_U32(0x2U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 5U));
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, 6U));  // wrapped

    fw_test_case_begin("BOUNCE never repeats the end channel back to back");
    for (step = 0U; step < 32U; step++)
    {
        uint32_t a = led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, step);
        uint32_t b = led_pattern_mask(LED_PATTERN_MODE_BOUNCE, 4U, step + 1U);
        FW_ASSERT_TRUE((a != b) || (4U == 1U));   // always moves to a neighbour
    }

    // ---------------------------------------------------------------- BLINK --
    fw_test_case_begin("BLINK alternates all-on / all-off");
    FW_ASSERT_EQ_U32(0x3U, led_pattern_mask(LED_PATTERN_MODE_BLINK, 2U, 0U));
    FW_ASSERT_EQ_U32(0x0U, led_pattern_mask(LED_PATTERN_MODE_BLINK, 2U, 1U));
    FW_ASSERT_EQ_U32(0x3U, led_pattern_mask(LED_PATTERN_MODE_BLINK, 2U, 2U));
    FW_ASSERT_EQ_U32(0xFU, led_pattern_mask(LED_PATTERN_MODE_BLINK, 4U, 0U));

    // ------------------------------------------------------------ BOUNDARY --
    fw_test_case_begin("invalid channel counts return 0 (no UB)");
    FW_ASSERT_EQ_U32(0U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 0U, 0U));
    FW_ASSERT_EQ_U32(0U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 33U, 0U));
    FW_ASSERT_EQ_U32(0U, led_pattern_mask(LED_PATTERN_MODE_BLINK, 0U, 5U));

    fw_test_case_begin("maximum channel count (32) is handled without UB");
    FW_ASSERT_EQ_U32(0xFFFFFFFFU, led_pattern_mask(LED_PATTERN_MODE_BLINK, 32U, 0U));
    FW_ASSERT_EQ_U32(0x1U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 32U, 0U));
    FW_ASSERT_EQ_U32(0x80000000U, led_pattern_mask(LED_PATTERN_MODE_CHASE, 32U, 31U));

    fw_test_case_begin("unknown mode yields an all-dark mask");
    FW_ASSERT_EQ_U32(0U, led_pattern_mask((led_pattern_mode_t)99, 2U, 0U));

    // ------------------------------------------------------------- POLARITY --
    fw_test_case_begin("active-low inverts the lit bits, active-high passes through");
    FW_ASSERT_EQ_U32(0x2U, led_pattern_levels(0x1U, 2U, true));    // bit0 lit -> GPIO0 low, GPIO1 high
    FW_ASSERT_EQ_U32(0x1U, led_pattern_levels(0x2U, 2U, true));
    FW_ASSERT_EQ_U32(0x0U, led_pattern_levels(0x3U, 2U, true));    // both lit -> both low
    FW_ASSERT_EQ_U32(0x3U, led_pattern_levels(0x0U, 2U, true));    // both dark -> both high
    FW_ASSERT_EQ_U32(0x1U, led_pattern_levels(0x1U, 2U, false));
    FW_ASSERT_EQ_U32(0x2U, led_pattern_levels(0x2U, 2U, false));

    fw_test_case_begin("levels ignore bits outside the channel count");
    FW_ASSERT_EQ_U32(0x2U, led_pattern_levels(0xFFFFFFFDU, 2U, true));
    FW_ASSERT_EQ_U32(0x0U, led_pattern_levels(0xFFFFFFFFU, 0U, true));

    // ------------------------------------------------------------ REFERENCE --
    fw_test_case_begin("FNV-1a reference matches the shared constant");
    FW_ASSERT_EQ_U32(LED_PATTERN_REF_FNV1A,
                     led_pattern_reference_fnv1a((uint16_t)LED_PATTERN_REF_CHANNELS));

    fw_test_case_begin("reference is stable and mode dependent");
    FW_ASSERT_TRUE(led_pattern_reference_fnv1a(2U) != led_pattern_reference_fnv1a(4U));
}
