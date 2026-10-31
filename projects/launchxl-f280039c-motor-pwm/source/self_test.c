//#############################################################################
//
// FILE:   self_test.c
//
// TITLE:  Power-on self test, pure-logic part (runs on target AND on host).
//
// DESCRIPTION:
//   All reference vectors below were computed BY HAND from the algorithms
//   documented in specs/0002-motor-pwm-skeleton/design.md (integer
//   arithmetic, quarter-wave sine table), NOT captured from a run of the
//   implementation - a broken implementation must fail here.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "self_test.h"
#include "angle_gen.h"
#include "svpwm.h"
#include "fmt.h"

volatile uint32_t g_self_test_code = (uint32_t)SELF_TEST_OK;

//
// SVPWM reference vectors (angle_x10, mag_permille) -> (a, b, c).
// Derivation (all integer math, table values T[0]=0, T[300]=500, T[600]=866,
// T[900]=1000):
//   (0,500)    alpha=500 beta=0    -> ta=500 tb=-250 tc=-250  -> (750,0,0)
//   (900,500)  alpha=0   beta=500  -> ta=0 tb=433 tc=-433     -> (433,866,0)
//   (1800,500) alpha=-500 beta=0   -> ta=-500 tb=250 tc=250   -> (0,750,750)
//   (2700,500) alpha=0   beta=-500 -> ta=0 tb=-433 tc=433     -> (433,0,866)
//   (300,500)  alpha=433 beta=250  -> ta=433 tb=0 tc=-433     -> (866,433,0)
//   (600,500)  alpha=250 beta=433  -> ta=250 tb=249 tc=-499   -> (749,748,0)
//   (0,800)    guard path: span=1200 -> scale -> (1000,0,0)
//   (0,0)      centred zero vector -> (500,500,500)
//
static const struct
{
    uint16_t angle_x10;
    uint16_t mag_permille;
    uint16_t a;
    uint16_t b;
    uint16_t c;
} self_test_svpwm_vectors[] = {
    { 0U,    500U, 750U,  0U,    0U   },
    { 900U,  500U, 433U,  866U,  0U   },
    { 1800U, 500U, 0U,    750U,  750U },
    { 2700U, 500U, 433U,  0U,    866U },
    { 300U,  500U, 866U,  433U,  0U   },
    { 600U,  500U, 749U,  748U,  0U   },
    { 0U,    800U, 1000U, 0U,    0U   },
    { 0U,    0U,   500U,  500U,  500U }
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

static bool self_test_duties_eq(const svpwm_duties_t *d, uint16_t a,
                                uint16_t b, uint16_t c)
{
    return ((d->a == a) && (d->b == b) && (d->c == c));
}

static self_test_result_t self_test_svpwm(void)
{
    svpwm_duties_t d;
    uint32_t i;

    for (i = 0U; i < (uint32_t)(sizeof(self_test_svpwm_vectors) /
                                sizeof(self_test_svpwm_vectors[0])); i++)
    {
        if (svpwm_modulate(self_test_svpwm_vectors[i].angle_x10,
                           self_test_svpwm_vectors[i].mag_permille, &d) !=
            SVPWM_OK)
        {
            return SELF_TEST_FAIL_SVPWM_QUAD;
        }
        if (!self_test_duties_eq(&d, self_test_svpwm_vectors[i].a,
                                 self_test_svpwm_vectors[i].b,
                                 self_test_svpwm_vectors[i].c))
        {
            if (self_test_svpwm_vectors[i].mag_permille == 0U)
            {
                return SELF_TEST_FAIL_SVPWM_ZERO;
            }
            if (self_test_svpwm_vectors[i].mag_permille > 666U)
            {
                return SELF_TEST_FAIL_SVPWM_GUARD;
            }
            return SELF_TEST_FAIL_SVPWM_QUAD;
        }
    }

    //
    // Line-voltage invariant on a linear-region vector: centring must not
    // change the difference between phases (da - db == ta - tb).
    // (600,500): ta - tb = 250 - 249 = 1, so a - b must be 1.
    //
    if (svpwm_modulate(600U, 500U, &d) != SVPWM_OK)
    {
        return SELF_TEST_FAIL_SVPWM_QUAD;
    }
    if ((uint16_t)(d.a - d.b) != 1U)
    {
        return SELF_TEST_FAIL_SVPWM_QUAD;
    }

    //
    // Illegal arguments must be rejected without touching the output.
    //
    d.a = 0xAAAAU;
    d.b = 0x5555U;
    d.c = 0xAAAAU;
    if (svpwm_modulate(0U, 1001U, &d) != SVPWM_ERR_MAG)
    {
        return SELF_TEST_FAIL_SVPWM_ARGS;
    }
    if ((d.a != 0xAAAAU) || (d.b != 0x5555U))
    {
        return SELF_TEST_FAIL_SVPWM_ARGS;
    }
    if (svpwm_modulate(0U, 500U, 0) != SVPWM_ERR_NULL)
    {
        return SELF_TEST_FAIL_SVPWM_ARGS;
    }

    return SELF_TEST_OK;
}

static self_test_result_t self_test_angle(void)
{
    angle_gen_t g;
    uint32_t i;

    //
    // 5.00 Hz, 20 ms ticks -> exactly 36 steps of 0.1 degree per tick.
    //
    angle_gen_init(&g, 500U);
    if (angle_gen_advance(&g, 20000U) != 360U)
    {
        return SELF_TEST_FAIL_ANGLE_STEP;
    }
    if (angle_gen_advance(&g, 2000U) != 396U)
    {
        return SELF_TEST_FAIL_ANGLE_STEP;
    }

    //
    // Rate change keeps the accumulated remainder exact:
    // 250.00 Hz, 1 ms -> 900 steps: 396 + 900 = 1296.
    //
    if (angle_gen_set_rate_hz_x100(&g, 25000U) != 0)
    {
        return SELF_TEST_FAIL_ANGLE_ARGS;
    }
    if (angle_gen_advance(&g, 1000U) != 1296U)
    {
        return SELF_TEST_FAIL_ANGLE_STEP;
    }

    //
    // Wrap-around: 10 x 20 ms at 5.00 Hz is exactly one full turn.
    //
    angle_gen_init(&g, 500U);
    for (i = 0U; i < 10U; i++)
    {
        (void)angle_gen_advance(&g, 20000U);
    }
    if (angle_gen_get(&g) != 0U)
    {
        return SELF_TEST_FAIL_ANGLE_WRAP;
    }

    //
    // Illegal rate is rejected and the previous rate is kept; a zero rate
    // parks the angle; a zero tick changes nothing.
    //
    if (angle_gen_set_rate_hz_x100(&g, 50001U) != -1)
    {
        return SELF_TEST_FAIL_ANGLE_ARGS;
    }
    if (angle_gen_advance(&g, 20000U) != 360U)
    {
        return SELF_TEST_FAIL_ANGLE_ARGS;   // rate must still be 500
    }
    angle_gen_init(&g, 0U);
    if (angle_gen_advance(&g, 1000U) != 0U)
    {
        return SELF_TEST_FAIL_ANGLE_STEP;
    }
    if (angle_gen_advance(0, 1000U) != 0U)
    {
        return SELF_TEST_FAIL_ANGLE_ARGS;
    }
    if (angle_gen_advance(&g, 0U) != 0U)
    {
        return SELF_TEST_FAIL_ANGLE_STEP;
    }

    return SELF_TEST_OK;
}

static self_test_result_t self_test_fmt(void)
{
    char buf[24];
    int32_t written;

    written = fmt_u32_dec(0U, buf, sizeof(buf));
    if ((written != 1) || !self_test_str_eq(buf, "0"))
    {
        return SELF_TEST_FAIL_FMT_DEC;
    }
    written = fmt_u32_dec(4294967295U, buf, sizeof(buf));
    if ((written != 10) || !self_test_str_eq(buf, "4294967295"))
    {
        return SELF_TEST_FAIL_FMT_DEC;
    }
    written = fmt_u32_hex(0x1A2BU, buf, sizeof(buf));
    if ((written != 6) || !self_test_str_eq(buf, "0x1A2B"))
    {
        return SELF_TEST_FAIL_FMT_HEX;
    }
    buf[0] = 'X';
    if (fmt_u32_dec(1000U, buf, 4U) != FMT_ERR_TOO_SMALL)
    {
        return SELF_TEST_FAIL_FMT_BOUND;
    }
    return SELF_TEST_OK;
}

self_test_result_t self_test_run(void)
{
    self_test_result_t result = self_test_svpwm();

    if (result == SELF_TEST_OK)
    {
        result = self_test_angle();
    }
    if (result == SELF_TEST_OK)
    {
        result = self_test_fmt();
    }

    g_self_test_code = (uint32_t)result;
    return result;
}
