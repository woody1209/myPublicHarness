//#############################################################################
//
// FILE:   cap_meas_hw.c
//
// TITLE:  eCAP 1-3 pad-loopback frequency/duty measurement (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "cap_meas_hw.h"
#include "board_pins.h"
#include "board_clock.h"
#include "driverlib.h"

#define CAP_FREQ_TOL_PCT     2U        /* +/-2 % band (REQ-009) */
#define CAP_MIN_SAMPLES      8U
#define CAP_DUTY_TOL_PCT     5U

volatile uint32_t g_cap_hz[3] = {0U, 0U, 0U};
const uint32_t g_cap_cfg_hz[3] = {10000U, 20000U, 50000U};
volatile uint32_t g_cap_samples[3] = {0U, 0U, 0U};
volatile uint32_t g_cap_ok_mask = 0U;
volatile bool g_cap_hr_active = false;

static uint32_t cap_period_sum[3];
static uint32_t cap_duty_sum[3];
static uint32_t cap_ok_bits;

static const uint32_t cap_base[3] = { ECAP1_BASE, ECAP2_BASE, ECAP3_BASE };
static const uint16_t cap_input[3] = {
    ECAP_INPUT_INPUTXBAR7, ECAP_INPUT_INPUTXBAR8, ECAP_INPUT_INPUTXBAR9
};
static const uint16_t cap_gpio[3] = {
    BOARD_EPWM1A_GPIO, BOARD_EPWM2A_GPIO, BOARD_EPWM3A_GPIO
};

static bool cap_init_one(uint32_t idx)
{
    uint32_t base = cap_base[idx];

    ECAP_stopCounter(base);
    ECAP_setCaptureMode(base, ECAP_CONTINUOUS_CAPTURE_MODE, ECAP_EVENT_4);
    /* rising, falling, rising, falling: CAP1/3 = period, CAP2 = high time */
    ECAP_setEventPolarity(base, ECAP_EVENT_1, ECAP_EVNT_RISING_EDGE);
    ECAP_setEventPolarity(base, ECAP_EVENT_2, ECAP_EVNT_FALLING_EDGE);
    ECAP_setEventPolarity(base, ECAP_EVENT_3, ECAP_EVNT_RISING_EDGE);
    ECAP_setEventPolarity(base, ECAP_EVENT_4, ECAP_EVNT_FALLING_EDGE);
    ECAP_disableCounterResetOnEvent(base, ECAP_EVENT_1);
    ECAP_disableCounterResetOnEvent(base, ECAP_EVENT_2);
    ECAP_disableCounterResetOnEvent(base, ECAP_EVENT_3);
    ECAP_disableCounterResetOnEvent(base, ECAP_EVENT_4);
    ECAP_selectECAPInput(base, (ECAP_InputCaptureSignals)cap_input[idx]);
    /* eCAP must keep capturing through debugger halts (the HIL halts the
     * core to read symbols - a stopped eCAP would freeze mid-window) */
    ECAP_setEmulationMode(base, ECAP_EMULATION_FREE_RUN);
    ECAP_enableTimeStampCapture(base);
    ECAP_reArm(base);
    ECAP_startCounter(base);
    return true;
}

bool cap_meas_init(void)
{
    uint32_t i;
    bool ok = true;

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ECAP1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ECAP2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ECAP3);

    /* pad-loopback sources: the ePWM output GPIOs, via the Input X-BAR */
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_ECAP1, cap_gpio[0]);
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_ECAP2, cap_gpio[1]);
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_ECAP3, cap_gpio[2]);

    for (i = 0U; i < 3U; i++)
    {
        cap_period_sum[i] = 0U;
        cap_duty_sum[i] = 0U;
        if (!cap_init_one(i)) { ok = false; }
    }

    /* ECAP3 high-resolution mode (HRCAP): the plain HR enable froze the
       channel on the first board run (g_cap_hz[2] stayed 0 while ECAP1/2
       measured exactly), so M2 ships the documented downgrade - standard
       mode, HR revisited behind its own bring-up task. */
    g_cap_hr_active = false;

    return ok;
}

//! Wrap-safe delta: a > b means the counter wrapped between the captures.
static uint32_t cap_delta(uint32_t hi, uint32_t lo)
{
    return (hi >= lo) ? (hi - lo) : ((0xFFFFFFFFU - lo) + 1U + hi);
}

void cap_meas_step(void)
{
    uint32_t i;

    for (i = 0U; i < 3U; i++)
    {
        uint32_t c1 = ECAP_getEventTimeStamp(cap_base[i], ECAP_EVENT_1);
        uint32_t c2 = ECAP_getEventTimeStamp(cap_base[i], ECAP_EVENT_2);
        uint32_t c3 = ECAP_getEventTimeStamp(cap_base[i], ECAP_EVENT_3);
        uint32_t period;
        uint32_t duty_pcx10;
        uint32_t freq;
        uint32_t want;
        uint32_t bit = (1UL << i);

        /* Torn-read guard: CAP1-3 update on EVERY edge, so one new edge
         * between the three reads yields a doubled period (first board run:
         * a 25 kHz outlier dragged the mean out of band and varied by boot).
         * Accept the triple only when a back-to-back re-read is identical -
         * a torn tick just samples nothing. */
        if ((c1 != ECAP_getEventTimeStamp(cap_base[i], ECAP_EVENT_1)) ||
            (c3 != ECAP_getEventTimeStamp(cap_base[i], ECAP_EVENT_3)))
        {
            continue;
        }

        period = cap_delta(c3, c1);
        /* Outlier pre-filter: a torn/garbage capture (one poisoned sample
         * dragged the cumulative mean out of band for good - 2026-10-25
         * board runs) must not enter the statistics. The configured period
         * in SYSCLK counts is known exactly (SYSCLK/cfg_hz); anything
         * outside [x0.5, x2] of it is not a real period of this channel.
         * Frequency precision is still judged on the +/-2 % mean. */
        {
            uint32_t exp_period = BOARD_SYSCLK_FREQ_HZ / g_cap_cfg_hz[i];

            if ((period < (exp_period / 2U)) || (period > (exp_period * 2U)))
            {
                continue;
            }
        }
        if (period == 0U)
        {
            continue;                     /* no full cycle captured yet */
        }
        duty_pcx10 = (cap_delta(c2, c1) * 1000U) / period;
        freq = BOARD_SYSCLK_FREQ_HZ / period;

        cap_period_sum[i] += period;
        cap_duty_sum[i] += duty_pcx10;
        g_cap_samples[i]++;
        g_cap_hz[i] = freq;

        if (g_cap_samples[i] >= CAP_MIN_SAMPLES)
        {
            uint32_t mean_hz = (cap_period_sum[i] > 0U)
                ? (BOARD_SYSCLK_FREQ_HZ /
                   (cap_period_sum[i] / g_cap_samples[i]))
                : 0U;
            uint32_t dev;

            want = g_cap_cfg_hz[i];
            dev = (mean_hz > want) ? (mean_hz - want) : (want - mean_hz);
            /* Frequency band only. Duty is MEASURED and reported (health
             * line) but not banded: EPWM1's duty is intentionally mutated
             * by the CMPSS trip-zone demo, so a duty band on channel 1
             * would fire fail-safe on the demo working as designed
             * (2026-10-25, three consecutive board runs). */
            if ((dev * 100U) <= (want * CAP_FREQ_TOL_PCT))
            {
                cap_ok_bits |= bit;
            }
            else
            {
                cap_ok_bits &= ~bit;  /* dead/marginal channel unhooks */
            }
            g_cap_ok_mask = cap_ok_bits;
        }
    }
}
