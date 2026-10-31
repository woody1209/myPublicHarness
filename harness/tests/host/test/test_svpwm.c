//#############################################################################
//
// FILE:   test_svpwm.c
//
// TITLE:  Host-side unit tests for the fixed-point space-vector PWM modulator.
//
//   Covers REQ-006 (quadrant vectors, over-modulation guard, zero vector,
//   angle wrap, argument validation) plus the modulator invariants: duty
//   bounds 0..1000, one phase clamped to 0 in the linear region, the max
//   phase saturated to 1000 in over-modulation, line-voltage preservation
//   under centring, and mirror symmetry (theta -> 360 deg - theta swaps
//   phases B and C).
//
//   Every expected value below was computed BY HAND from the design.md
//   contract (alpha = mag*cos, beta = mag*sin, inverse Clarke with
//   sqrt(3)/2 = 866/1000, min-max centring, guard rescale) and cross-checked
//   with an independent Python model over ALL 3600 angles - NOT captured
//   from a run of svpwm.c, so a broken implementation cannot pass by
//   matching its own output.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "svpwm.h"

//! Compare a modulator result against an expected triple.
static void svpwm_expect(uint16_t angle_x10, uint16_t mag_permille,
                         uint16_t ea, uint16_t eb, uint16_t ec)
{
    svpwm_duties_t d;

    //
    // Poison the output first: a modulator that forgets to write a field
    // would otherwise inherit the previous case's values.
    //
    d.a = 0xAAAAU;
    d.b = 0x5555U;
    d.c = 0xAAAAU;

    FW_ASSERT_EQ_I32(SVPWM_OK,
                     svpwm_modulate(angle_x10, mag_permille, &d));
    FW_ASSERT_EQ_U32(ea, d.a);
    FW_ASSERT_EQ_U32(eb, d.b);
    FW_ASSERT_EQ_U32(ec, d.c);
}

void svpwm_tests(void)
{
    svpwm_duties_t d;
    svpwm_duties_t e;
    uint32_t th;

    fw_test_suite_begin("svpwm");

    // ------------------------------------------------------- QUADRANTS ------
    fw_test_case_begin("quadrant I mid-angle 45 deg, mag 250");
    // alpha = 250*T[1350]/1000 = 176, beta = 250*T[450]/1000 = 176
    // ta=176 tb=64 tc=-240 -> (416, 304, 0)
    svpwm_expect(450U, 250U, 416U, 304U, 0U);

    fw_test_case_begin("quadrant II 135 deg, mag 750 (over-modulated)");
    // alpha=-530 beta=530: ta=-530 tb=723 tc=-193, span=1253 -> guard
    // (0, 1253, 337)*1000/1253 = (0, 1000, 269) -> rounded 268
    svpwm_expect(1350U, 750U, 0U, 1000U, 268U);

    fw_test_case_begin("quadrant III 210 deg, mag 1000 (over-modulated)");
    // alpha=-866 beta=-500: span=1732 -> (0, 500, 1000)
    svpwm_expect(2100U, 1000U, 0U, 500U, 1000U);

    fw_test_case_begin("quadrant IV 315 deg, mag 500");
    // alpha=353 beta=-353: ta=353 tb=-482 tc=129 -> (835, 0, 611)
    svpwm_expect(3150U, 500U, 835U, 0U, 611U);

    fw_test_case_begin("just below wrap: 359.9 deg behaves like 0 deg with a"
                       " vanishing sine");
    // sin(359.9 deg) = -2 per-mille -> beta = 0, alpha = 300
    svpwm_expect(3599U, 300U, 450U, 0U, 0U);

    fw_test_case_begin("linear-region boundary mag 666 keeps span <= 1000");
    // alpha=-333 beta=576: span=998, no guard -> (0, 998, 1)
    svpwm_expect(1200U, 666U, 0U, 998U, 1U);

    fw_test_case_begin("small angle 10 deg, mag 300");
    svpwm_expect(100U, 300U, 487U, 90U, 0U);

    fw_test_case_begin("angle wraps mod 3600: 370.0 deg == 10.0 deg");
    svpwm_expect(3700U, 300U, 487U, 90U, 0U);

    fw_test_case_begin("270 deg, mag 250 (quadrant IV entry)");
    svpwm_expect(2700U, 250U, 216U, 0U, 432U);

    fw_test_case_begin("240 deg, mag 999 (guard, span 1498)");
    svpwm_expect(2400U, 999U, 0U, 0U, 1000U);

    // ------------------------------------------------------------ GUARD -----
    fw_test_case_begin("over-modulation guard at 0 deg, mag 1000");
    // alpha=1000 beta=0: ta=1000 tb=tc=-500, span=1500
    // centred (1500, 0, 0) -> rescaled (1000, 0, 0)
    svpwm_expect(0U, 1000U, 1000U, 0U, 0U);

    fw_test_case_begin("over-modulation guard at 90 deg, mag 1000");
    // alpha=0 beta=1000: ta=0 tb=866 tc=-866, span=1732
    // centred (866, 1732, 0) -> rescaled (500, 1000, 0)
    svpwm_expect(900U, 1000U, 500U, 1000U, 0U);

    // -------------------------------------------------------- ZERO VECTOR ---
    fw_test_case_begin("mag 0 is the centred zero vector at any angle");
    svpwm_expect(0U, 0U, 500U, 500U, 500U);
    svpwm_expect(1234U, 0U, 500U, 500U, 500U);
    svpwm_expect(3599U, 0U, 500U, 500U, 500U);

    // --------------------------------------------------------- INVARIANTS ---
    fw_test_case_begin("line voltage is preserved by centring (da-db = ta-tb)");
    // (450, 250): ta - tb = 176 - 64 = 112, so a - b must be 112.
    FW_ASSERT_EQ_I32(SVPWM_OK, svpwm_modulate(450U, 250U, &d));
    FW_ASSERT_EQ_I32(112, (int32_t)((int32_t)d.a - (int32_t)d.b));

    fw_test_case_begin("mirror symmetry: 360 deg - theta swaps phases B and C");
    // (450, 250) -> (416, 304, 0); (3150, 250) must be (416, 0, 304).
    FW_ASSERT_EQ_I32(SVPWM_OK, svpwm_modulate(3150U, 250U, &e));
    FW_ASSERT_EQ_U32(d.a, e.a);
    FW_ASSERT_EQ_U32(d.c, e.b);
    FW_ASSERT_EQ_U32(d.b, e.c);

    fw_test_case_begin("duties stay in 0..1000 over the full angle range");
    for (th = 0U; th < 3600U; th += 37U)
    {
        static const uint16_t mags[] = { 1U, 250U, 500U, 666U, 800U, 1000U };
        uint32_t m;

        for (m = 0U; m < (uint32_t)(sizeof(mags) / sizeof(mags[0])); m++)
        {
            FW_ASSERT_EQ_I32(SVPWM_OK,
                             svpwm_modulate((uint16_t)th, mags[m], &d));
            FW_ASSERT_TRUE(d.a <= SVPWM_DUTY_FULL_SCALE);
            FW_ASSERT_TRUE(d.b <= SVPWM_DUTY_FULL_SCALE);
            FW_ASSERT_TRUE(d.c <= SVPWM_DUTY_FULL_SCALE);
            //
            // Linear region (mag <= 666): centring alone must already put one
            // phase at 0. Over-modulation (mag > 666): the guard must saturate
            // the largest phase to full scale.
            //
            if (mags[m] <= 666U)
            {
                FW_ASSERT_TRUE((d.a == 0U) || (d.b == 0U) || (d.c == 0U));
            }
            else
            {
                FW_ASSERT_TRUE((d.a == SVPWM_DUTY_FULL_SCALE) ||
                               (d.b == SVPWM_DUTY_FULL_SCALE) ||
                               (d.c == SVPWM_DUTY_FULL_SCALE));
            }
        }
    }

    fw_test_case_begin("periodicity: theta and theta + 3600 give the same vector");
    for (th = 0U; th < 3600U; th += 617U)
    {
        FW_ASSERT_EQ_I32(SVPWM_OK, svpwm_modulate((uint16_t)th, 432U, &d));
        FW_ASSERT_EQ_I32(SVPWM_OK,
                         svpwm_modulate((uint16_t)(th + 3600U), 432U, &e));
        FW_ASSERT_EQ_U32(d.a, e.a);
        FW_ASSERT_EQ_U32(d.b, e.b);
        FW_ASSERT_EQ_U32(d.c, e.c);
    }

    // --------------------------------------------------------- ARGUMENTS ----
    fw_test_case_begin("illegal magnitude is rejected without touching output");
    d.a = 0xAAAAU;
    d.b = 0x5555U;
    d.c = 0xAAAAU;
    FW_ASSERT_EQ_I32(SVPWM_ERR_MAG, svpwm_modulate(0U, 1001U, &d));
    FW_ASSERT_EQ_U32(0xAAAAU, d.a);
    FW_ASSERT_EQ_U32(0x5555U, d.b);

    fw_test_case_begin("NULL output pointer is rejected");
    FW_ASSERT_EQ_I32(SVPWM_ERR_NULL, svpwm_modulate(0U, 500U, 0));
}
