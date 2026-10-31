//#############################################################################
//
// FILE:   svpwm.c
//
// TITLE:  Space-vector PWM modulator (PURE LOGIC, host-testable).
//
// DESCRIPTION:
//   Given the electrical angle and a magnitude, produce three per-mille phase
//   duties using min-max centring (zero-sequence injection):
//
//     alpha = mag * cos(theta)
//     beta  = mag * sin(theta)
//     ta = alpha
//     tb = -0.5*alpha + 0.866*beta
//     tc = -0.5*alpha - 0.866*beta
//     off = min(ta, tb, tc)
//     da/db/dc = (ta-off), (tb-off), (tc-off)          // centring
//     if (max-min > 1000) scale all three by 1000/(max-min)   // guard
//     mag == 0 -> 500/500/500 (centred zero vector)
//
//   Centring shifts only the common mode: da-db == ta-tb exactly (asserted
//   by the host tests). In the linear region (mag <= 666 per-mille) the
//   centring alone keeps every duty inside 0..1000; above that the guard
//   rescales so the duties stay representable.
//
//   All arithmetic is integer; sin/cos come from the quarter-wave table
//   (svpwm_table.h, 0.1 degree resolution).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "svpwm.h"
#include "svpwm_table.h"

//! sqrt(3)/2 in per-mille: 0.8660254 -> 866.
#define SVPWM_SQRT3_HALF_X1000          866

//! sin(x) in per-mille for x in tenths of a degree (any range, wraps).
static int32_t svpwm_sin_x1000(uint16_t angle_x10)
{
    uint32_t a = (uint32_t)(angle_x10 % SVPWM_TABLE_FULL_STEPS);
    uint32_t quad = a / SVPWM_TABLE_QUARTER_STEPS;   // 0..3
    uint32_t q = a % SVPWM_TABLE_QUARTER_STEPS;      // 0..899
    int32_t v;

    if ((quad == 0U) || (quad == 2U))
    {
        v = (int32_t)svpwm_sin_q[q];
    }
    else
    {
        v = (int32_t)svpwm_sin_q[SVPWM_TABLE_QUARTER_STEPS - q];
    }
    if (quad >= 2U)
    {
        v = -v;
    }
    return v;
}

//! Clamp helper.
static uint16_t svpwm_clamp_u16(int32_t v)
{
    if (v < 0)
    {
        return 0U;
    }
    if (v > (int32_t)SVPWM_DUTY_FULL_SCALE)
    {
        return SVPWM_DUTY_FULL_SCALE;
    }
    return (uint16_t)v;
}

int svpwm_modulate(uint16_t angle_x10, uint16_t mag_permille,
                   svpwm_duties_t *out)
{
    int32_t alpha, beta;
    int32_t ta, tb, tc;
    int32_t tmin, tmax, span;
    int32_t da, db, dc;

    if (out == 0)
    {
        return SVPWM_ERR_NULL;
    }
    if (mag_permille > SVPWM_DUTY_FULL_SCALE)
    {
        return SVPWM_ERR_MAG;
    }

    //
    // Zero magnitude is the centred zero vector: all three phases at 50%
    // duty (spec 0002 design.md SVPWM section). min-max centring alone would
    // degenerate to the 000 zero vector here; the mid vector keeps the
    // phase voltages visibly centred on a scope and is the conventional
    // "no output" state of a modulator.
    //
    if (mag_permille == 0U)
    {
        out->a = SVPWM_DUTY_MID;
        out->b = SVPWM_DUTY_MID;
        out->c = SVPWM_DUTY_MID;
        return SVPWM_OK;
    }

    //
    // alpha = mag * cos(theta) = mag * sin(theta + 90 deg), per-mille.
    //
    alpha = ((int32_t)mag_permille *
             svpwm_sin_x1000((uint16_t)(angle_x10 +
                                        SVPWM_TABLE_QUARTER_STEPS))) / 1000;
    beta  = ((int32_t)mag_permille * svpwm_sin_x1000(angle_x10)) / 1000;

    //
    // Inverse Clarke (per-mille): tb/tc use sqrt(3)/2 = 866/1000.
    //
    ta = alpha;
    tb = (-500 * alpha + SVPWM_SQRT3_HALF_X1000 * beta) / 1000;
    tc = (-500 * alpha - SVPWM_SQRT3_HALF_X1000 * beta) / 1000;

    //
    // min-max centring: subtract the minimum so all duties are >= 0.
    //
    tmin = ta;
    if (tb < tmin) { tmin = tb; }
    if (tc < tmin) { tmin = tc; }
    tmax = ta;
    if (tb > tmax) { tmax = tb; }
    if (tc > tmax) { tmax = tc; }

    da = ta - tmin;
    db = tb - tmin;
    dc = tc - tmin;
    span = tmax - tmin;

    //
    // Over-modulation guard: rescale so the span fits 0..1000. Scaling is
    // applied to all three phases with the same divisor, so the line
    // voltages keep their ratio (up to +/-1 per-mille rounding).
    //
    if (span > (int32_t)SVPWM_DUTY_FULL_SCALE)
    {
        da = (da * (int32_t)SVPWM_DUTY_FULL_SCALE) / span;
        db = (db * (int32_t)SVPWM_DUTY_FULL_SCALE) / span;
        dc = (dc * (int32_t)SVPWM_DUTY_FULL_SCALE) / span;
    }

    out->a = svpwm_clamp_u16(da);
    out->b = svpwm_clamp_u16(db);
    out->c = svpwm_clamp_u16(dc);
    return SVPWM_OK;
}
