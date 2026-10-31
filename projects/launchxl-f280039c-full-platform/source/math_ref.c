//#############################################################################
//
// FILE:   math_ref.c
//
// TITLE:  Pure-C math references (host-testable mirrors of TMU workloads).
//
// DESIGN:
//   double-precision Taylor series with argument reduction - independent of
//   the TMU hardware the target judgement compares against. double is
//   software-emulated on C28x (FPU32), which is fine at the 1 Hz judgement
//   task rate. Accuracy: < 1e-5 abs vs libm on the tested domains.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "math_ref.h"

#define MATH_REF_PI       3.14159265358979323846
#define MATH_REF_HALF_PI  1.57079632679489661923
#define MATH_REF_HALF_PIF 1.57079632679489661923f

//! Newton-free sqrt for the halving identity: |s| <= 1 here, so 1+s*s <= 2
//! and a few Newton iterations from 1.0 converge to double precision.
static double math_sqrt_local(double v)
{
    double x = 1.0;
    uint16_t i;

    for (i = 0U; i < 24U; i++)
    {
        x = 0.5 * (x + v / x);
    }
    return x;
}

//! atan(t) for |t| <= 1: halve the argument with
//! atan(s) = 2*atan(s / (1 + sqrt(1+s*s))), then Taylor.
static double math_atan_unit(double s)
{
    double t;
    double t2;
    double acc;
    int32_t neg = (s < 0.0) ? 1 : 0;

    if (neg) { s = -s; }
    t = s / (1.0 + math_sqrt_local(1.0 + s * s));
    t2 = t * t;
    acc = t * (1.0 - t2 / 3.0 + (t2 * t2) / 5.0 - (t2 * t2 * t2) / 7.0 +
               (t2 * t2 * t2 * t2) / 9.0 - (t2 * t2 * t2 * t2 * t2) / 11.0);
    acc = 2.0 * acc;
    return neg ? -acc : acc;
}

//! atan(s) for any finite s (odd function, |s| > 1 folded by pi/2 - atan(1/s)).
static double math_atan(double s)
{
    double a;

    if (s > 1.0)
    {
        a = MATH_REF_HALF_PI - math_atan_unit(1.0 / s);
    }
    else if (s < -1.0)
    {
        a = -MATH_REF_HALF_PI - math_atan_unit(1.0 / s);
    }
    else
    {
        a = math_atan_unit(s);
    }
    return a;
}

float math_ref_sin_f32(float xf)
{
    double x = (double)xf;
    /* reduce to r in [-pi/4, +pi/4]: q2 = round(x / (pi/2)) */
    double q = x * (2.0 / MATH_REF_PI);
    int32_t qi = (int32_t)(q + ((q < 0.0) ? -0.5 : 0.5));
    double r = x - ((double)qi * (MATH_REF_PI / 2.0));
    double r2 = r * r;
    double sin_r = r * (1.0 - r2 / 6.0 + (r2 * r2) / 120.0 -
                        (r2 * r2 * r2) / 5040.0);
    double cos_r = 1.0 - r2 / 2.0 + (r2 * r2) / 24.0 - (r2 * r2 * r2) / 720.0;
    int32_t qm = ((qi % 4) + 4) % 4;
    double out;

    switch (qm)
    {
        case 1:  out = cos_r;  break;      /* sin(pi/2 + r) =  cos(r)  */
        case 2:  out = -sin_r; break;      /* sin(pi   + r) = -sin(r)  */
        case 3:  out = -cos_r; break;      /* sin(3pi/2 + r) = -cos(r) */
        default: out = sin_r;  break;      /* sin(0    + r) =  sin(r)  */
    }
    return (float)out;
}

float math_ref_atan2_f32(float yf, float xf)
{
    double y = (double)yf;
    double x = (double)xf;
    double a;

    if ((x == 0.0) && (y == 0.0))
    {
        return 0.0f;
    }
    if (x == 0.0)
    {
        return (y > 0.0) ? MATH_REF_HALF_PIF : -MATH_REF_HALF_PIF;
    }

    a = math_atan(y / x);
    if (x < 0.0)
    {
        a = (y >= 0.0) ? (a + MATH_REF_PI) : (a - MATH_REF_PI);
    }
    return (float)a;
}

int16_t math_ref_sin_q15(uint16_t index)
{
    double x = ((double)(index & 0xFFU) * 2.0 * MATH_REF_PI) / 256.0;
    double s = (double)math_ref_sin_f32((float)x);

    if (s >= 1.0) { return (int16_t)32767; }
    if (s <= -1.0) { return (int16_t)-32768; }
    return (int16_t)(s * 32768.0);
}
