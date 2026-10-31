//#############################################################################
//
// FILE:   test_math_ref.c
//
// TITLE:  Host unit tests for math_ref against libm (spec 0002 T-012).
//
// DESCRIPTION:
//   math_ref mirrors the TMU workloads in pure C. On the host the reference
//   itself is judged against libm; on the target the TMU result is judged
//   against math_ref. The 5e-4 band absorbs the polynomial truncation plus
//   float32 rounding of the host comparison.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "math_ref.h"

#include <math.h>

#define TOL_SIN     5e-4f
#define TOL_ATAN2   5e-4f

static bool close_f32(float a, float b, float tol)
{
    float d = a - b;

    return ((d < 0.0f) ? -d : d) <= tol;
}

void math_ref_tests(void)
{
    fw_test_suite_begin("math_ref (vs libm)");

    fw_test_case_begin("sin: quadrant and range-reduction points");
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(0.0f), 0.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(0.5235988f), 0.5f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(0.7853982f), 0.70710678f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(1.5707963f), 1.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(3.1415927f), 0.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(4.7123890f), -1.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(6.2831853f), 0.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(-1.5707963f), -1.0f, TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(100.7f), sinf(100.7f), TOL_SIN));
    FW_ASSERT_TRUE(close_f32(math_ref_sin_f32(-250.25f), sinf(-250.25f), TOL_SIN));

    fw_test_case_begin("atan2: octants and axes");
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(1.0f, 1.0f), 0.7853982f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(-1.0f, 1.0f), -0.7853982f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(-1.0f, -1.0f), -2.3561945f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(1.0f, -1.0f), 2.3561945f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(1.0f, 0.0f), 1.5707963f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(0.0f, 1.0f), 0.0f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(0.0f, -1.0f), 3.1415927f, TOL_ATAN2));
    FW_ASSERT_TRUE(close_f32(math_ref_atan2_f32(0.37f, -1.42f),
                             atan2f(0.37f, -1.42f), TOL_ATAN2));

    fw_test_case_begin("q15 sine table: cardinal points");
    FW_ASSERT_EQ_I32(0, (int32_t)math_ref_sin_q15(0U));
    FW_ASSERT_TRUE((math_ref_sin_q15(64U) > 32700) &&
                   (math_ref_sin_q15(64U) <= 32767));
    FW_ASSERT_TRUE((math_ref_sin_q15(128U) < 100) &&
                   (math_ref_sin_q15(128U) > -100));
    FW_ASSERT_TRUE((math_ref_sin_q15(192U) < -32700) &&
                   (math_ref_sin_q15(192U) >= -32768));
    FW_ASSERT_TRUE(math_ref_sin_q15(32U) > 22500);       /* ~23170 */
    FW_ASSERT_TRUE(math_ref_sin_q15(32U) < 23900);
    FW_ASSERT_EQ_I32((int32_t)math_ref_sin_q15(0U),
                     (int32_t)math_ref_sin_q15(256U));   /* wrap */
}
