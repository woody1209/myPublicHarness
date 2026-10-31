//#############################################################################
//
// FILE:   test_angle_gen.c
//
// TITLE:  Host-side unit tests for the open-loop electrical angle generator.
//
//   Covers REQ-005 (rate bounds, exact rational stepping, wrap-around, NULL /
//   zero-tick behaviour, 64-bit headroom, associativity of tick splitting).
//   Every expected value below was computed BY HAND from the accumulator
//   contract in specs/0002-motor-pwm-skeleton/design.md
//   (steps = rate*us*3600 / 1e8, angle mod 3600) and cross-checked with an
//   independent Python model - NOT captured from a run of angle_gen.c, so a
//   broken implementation cannot pass by matching its own output.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "angle_gen.h"

void angle_gen_tests(void)
{
    angle_gen_t g;
    angle_gen_t h;
    uint32_t i;

    fw_test_suite_begin("angle_gen");

    // ------------------------------------------------------- BASIC STEPPING --
    fw_test_case_begin("5.00 Hz over 20 ms advances exactly 36.0 degrees");
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_U32(360U, angle_gen_advance(&g, 20000U));
    FW_ASSERT_EQ_U32(360U, angle_gen_get(&g));

    fw_test_case_begin("sub-step continuity: +2 ms adds 3.6 degrees");
    FW_ASSERT_EQ_U32(396U, angle_gen_advance(&g, 2000U));

    fw_test_case_begin("mid-stream rate change keeps the remainder exact");
    // 250.00 Hz for 1 ms is exactly 900 tenths; 396 + 900 = 1296.
    FW_ASSERT_EQ_I32(0, angle_gen_set_rate_hz_x100(&g, 25000U));
    FW_ASSERT_EQ_U32(1296U, angle_gen_advance(&g, 1000U));

    // ------------------------------------------------------------- WRAP -----
    fw_test_case_begin("10 x 20 ms at 5.00 Hz is exactly one full turn");
    angle_gen_init(&g, 500U);
    for (i = 0U; i < 10U; i++)
    {
        (void)angle_gen_advance(&g, 20000U);
    }
    FW_ASSERT_EQ_U32(0U, angle_gen_get(&g));

    // --------------------------------------------------------- RATE BOUNDS --
    fw_test_case_begin("init clamps an illegal rate to 500.00 Hz");
    angle_gen_init(&g, 60000U);
    FW_ASSERT_EQ_U32(1800U, angle_gen_advance(&g, 1000U));   // 500 Hz x 1 ms = 180 deg

    fw_test_case_begin("set_rate rejects > 500.00 Hz and keeps the old rate");
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_I32(-1, angle_gen_set_rate_hz_x100(&g, 50001U));
    FW_ASSERT_EQ_U32(360U, angle_gen_advance(&g, 20000U));   // still 5.00 Hz

    fw_test_case_begin("set_rate accepts the 500.00 Hz boundary");
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_I32(0, angle_gen_set_rate_hz_x100(&g, 50000U));
    FW_ASSERT_EQ_U32(1800U, angle_gen_advance(&g, 1000U));   // 500 Hz x 1 ms

    fw_test_case_begin("rate 0 parks the angle");
    angle_gen_init(&g, 0U);
    FW_ASSERT_EQ_U32(0U, angle_gen_advance(&g, 1000U));
    angle_gen_init(&g, 500U);
    (void)angle_gen_advance(&g, 20000U);                     // 360
    FW_ASSERT_EQ_I32(0, angle_gen_set_rate_hz_x100(&g, 0U));
    FW_ASSERT_EQ_U32(360U, angle_gen_advance(&g, 20000U));   // parked

    // --------------------------------------------------------- PRECISION ----
    fw_test_case_begin("fractional rate 3.33 Hz over 1 s lands on 118.8 deg");
    angle_gen_init(&g, 333U);
    FW_ASSERT_EQ_U32(1188U, angle_gen_advance(&g, 1000000U));

    fw_test_case_begin("slowest rate 0.01 Hz over 1 s advances 3.6 deg");
    angle_gen_init(&g, 1U);
    FW_ASSERT_EQ_U32(36U, angle_gen_advance(&g, 1000000U));

    fw_test_case_begin("max rate 500 Hz over 3.6 ms wraps once (288.0 deg)");
    angle_gen_init(&g, 50000U);
    FW_ASSERT_EQ_U32(2880U, angle_gen_advance(&g, 3600U));

    fw_test_case_begin("huge tick exercises the 64-bit accumulator");
    // 500 x 4123456789 x 3600 / 1e8 = 74222222.202 steps; mod 3600 -> 1022.
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_U32(1022U, angle_gen_advance(&g, 4123456789U));

    // ------------------------------------------------------ ASSOCIATIVITY ---
    fw_test_case_begin("one 1234567 us tick equals 1234 x 1000 us + 567 us");
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_U32(622U, angle_gen_advance(&g, 1234567U));
    angle_gen_init(&h, 500U);
    for (i = 0U; i < 1234U; i++)
    {
        (void)angle_gen_advance(&h, 1000U);
    }
    FW_ASSERT_EQ_U32(622U, angle_gen_advance(&h, 567U));

    fw_test_case_begin("one 12345 us tick equals 12 x 1028 us + 9 us");
    angle_gen_init(&g, 500U);
    FW_ASSERT_EQ_U32(222U, angle_gen_advance(&g, 12345U));
    angle_gen_init(&h, 500U);
    for (i = 0U; i < 12U; i++)
    {
        (void)angle_gen_advance(&h, 1028U);
    }
    FW_ASSERT_EQ_U32(222U, angle_gen_advance(&h, 9U));

    fw_test_case_begin("associativity holds across the rate range");
    // Same total elapsed time (1234567 us) reached in one tick vs 1235 ticks
    // must land on the same angle for every rate - no step may be lost.
    {
        static const uint32_t rates[] = { 1U, 7U, 333U, 500U, 25000U, 50000U };
        for (i = 0U; i < (uint32_t)(sizeof(rates) / sizeof(rates[0])); i++)
        {
            uint32_t k;

            angle_gen_init(&g, rates[i]);
            (void)angle_gen_advance(&g, 1234567U);
            angle_gen_init(&h, rates[i]);
            for (k = 0U; k < 1234U; k++)
            {
                (void)angle_gen_advance(&h, 1000U);
            }
            (void)angle_gen_advance(&h, 567U);
            FW_ASSERT_EQ_U32(angle_gen_get(&g), angle_gen_get(&h));
        }
    }

    // ------------------------------------------------------------ EDGE ------
    fw_test_case_begin("zero tick and NULL state are no-ops, never UB");
    angle_gen_init(&g, 500U);
    (void)angle_gen_advance(&g, 20000U);                 // 360
    FW_ASSERT_EQ_U32(360U, angle_gen_advance(&g, 0U));
    FW_ASSERT_EQ_U32(0U, angle_gen_advance(0, 1000U));
    FW_ASSERT_EQ_U32(0U, angle_gen_get(0));
    FW_ASSERT_EQ_I32(-1, angle_gen_set_rate_hz_x100(0, 500U));
    FW_ASSERT_EQ_U32(360U, angle_gen_get(&g));           // untouched
}
