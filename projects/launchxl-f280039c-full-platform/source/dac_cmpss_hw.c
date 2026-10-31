//#############################################################################
//
// FILE:   dac_cmpss_hw.c
//
// TITLE:  DAC -> ADC loopback + CMPSS1/TZ demo (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "dac_cmpss_hw.h"
#include "adc_scan_hw.h"
#include "board_pins.h"
#include "driverlib.h"

//! DAC staircase codes: 8 levels across the 12-bit range.
#define DAC_STEPCODE(i)   ((uint16_t)(((uint32_t)(i) * 4095U) / (DAC_STEPS - 1U)))
//! DAC vs ADC readback tolerance (codes): gain/offset of the buffer stage.
#define DAC_LB_TOL        128U
//! CMPSS threshold high enough that a low DACA keeps the comparator low and
//! a high DACA trips it (code 2048 = mid-scale).
#define CMPSS_THRESH      2048U
//! Delay loops for analog settle during discovery.
#define DAC_SETTLE_LOOPS  ((uint32_t)(120000000U / 10000U))

static uint16_t dac_step_index;
static uint16_t dac_lb_ch_a;         /* discovered ADCA channel for DACA */
static uint16_t dac_lb_ch_b;         /* discovered ADCC channel for DACB */
static uint16_t dac_mux_found;       /* CMPHPMUX value for CMPSS1, 0xFF = none */
static bool     dac_bound;           /* adc_scan SOCs rebound after discovery */

volatile uint32_t g_dac_steps_ok = 0U;
volatile uint32_t g_dac_steps_err = 0U;
volatile uint32_t g_cmpss_high_hits = 0U;
volatile uint32_t g_tz_hits = 0U;

//! One software conversion on a temporary SOC of module 'base' at 'ch'.
static uint16_t dac_probe_convert(uint32_t base, uint32_t soc, uint16_t ch)
{
    ADC_setupSOC(base, (ADC_SOCNumber)soc, ADC_TRIGGER_SW_ONLY,
                 (ADC_Channel)ch, 75U);
    ADC_forceSOC(base, (ADC_SOCNumber)soc);
    while (ADC_isBusy(base))
    {
        /* bounded: one conversion at 75 acqps is ~2 us */
    }
    return ADC_readResult((base == ADCA_BASE) ? ADCARESULT_BASE :
                          (base == ADCB_BASE) ? ADCBRESULT_BASE :
                                                ADCCRESULT_BASE,
                          (ADC_SOCNumber)soc);
}

//! Find the ADCA/ADCC channel that carries the DACA/DACB output: the channel
//! reading low with DACA=200 and high with DACA=3900.
static uint16_t dac_find_loopback_ch(uint32_t adc_base, uint32_t dac_base,
                                     uint32_t probe_soc)
{
    uint16_t ch;
    uint16_t found = 0xFFU;

    DAC_setShadowValue(dac_base, 200U);
    SysCtl_delay(DAC_SETTLE_LOOPS);
    for (ch = 0U; ch <= 15U; ch++)
    {
        if (dac_probe_convert(adc_base, probe_soc, ch) < 600U)
        {
            /* low at DAC low - check the high level on the same channel */
            DAC_setShadowValue(dac_base, 3900U);
            SysCtl_delay(DAC_SETTLE_LOOPS);
            if (dac_probe_convert(adc_base, probe_soc, ch) > 3500U)
            {
                found = ch;
            }
        }
    }
    DAC_setShadowValue(dac_base, 2048U);
    SysCtl_delay(DAC_SETTLE_LOOPS);
    return found;
}

//! Sweep CMPHPMUX for CMPSS1: the winning value is the one whose comparator
//! high status tracks the DACA staircase (low -> not tripped, high -> tripped).
static uint16_t dac_find_cmpss_mux(void)
{
    uint16_t v;
    uint16_t found = 0xFFU;

    if (dac_lb_ch_a == 0xFFU)
    {
        return 0xFFU;                    /* no DACA output node to compare */
    }

    for (v = 0U; v <= 5U; v++)
    {
        ASysCtl_selectCMPHPMux(ASYSCTL_CMPHPMUX_SELECT_1, v);

        DAC_setShadowValue(DACA_BASE, 100U);
        SysCtl_delay(DAC_SETTLE_LOOPS);
        if ((CMPSS_getStatus(CMPSS1_BASE) & CMPSS_STS_HI_FILTOUT) != 0U)
        {
            continue;                    /* high with a low input: not ours */
        }
        DAC_setShadowValue(DACA_BASE, 3900U);
        SysCtl_delay(DAC_SETTLE_LOOPS);
        if ((CMPSS_getStatus(CMPSS1_BASE) & CMPSS_STS_HI_FILTOUT) != 0U)
        {
            found = v;
            break;
        }
    }
    DAC_setShadowValue(DACA_BASE, 0U);
    SysCtl_delay(DAC_SETTLE_LOOPS);
    return found;
}

bool dac_cmpss_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_DACA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_DACB);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CMPSS1);

    // Internal DAC->ADC analog loopback: DACA feeds ADCA, DACB feeds ADCC.
    ASysCtl_enableADCDACLoopback(ASYSCTL_ADCDACLOOPBACK_ENLB2ADCA |
                                 ASYSCTL_ADCDACLOOPBACK_ENLB2ADCC);

    DAC_setReferenceVoltage(DACA_BASE, DAC_REF_ADC_VREFHI);
    DAC_setReferenceVoltage(DACB_BASE, DAC_REF_ADC_VREFHI);
    DAC_setGainMode(DACA_BASE, DAC_GAIN_ONE);
    DAC_setGainMode(DACB_BASE, DAC_GAIN_ONE);
    DAC_setLoadMode(DACA_BASE, DAC_LOAD_SYSCLK);
    DAC_setLoadMode(DACB_BASE, DAC_LOAD_SYSCLK);
    DAC_enableOutput(DACA_BASE);
    DAC_enableOutput(DACB_BASE);

    dac_lb_ch_a = dac_find_loopback_ch(ADCA_BASE, DACA_BASE, 2U);
    dac_lb_ch_b = dac_find_loopback_ch(ADCC_BASE, DACB_BASE, 2U);
    adc_scan_bind_dac_channels(dac_lb_ch_a, dac_lb_ch_b);
    dac_bound = true;

    // CMPSS1: internal reference DAC as the inverting input, threshold mid.
    CMPSS_configDAC(CMPSS1_BASE, CMPSS_DACSRC_SHDW);
    // Threshold 4095 FIRST: the CMPHPMUX sits at reset (floating analog pin)
    // until the sweep runs, and a floating input above the default threshold
    // trips CTRIPH - which, once wired, pinned EPWM1A low and starved the
    // ECAP1 pad loopback (diag21, 2026-10-25). Raise the bar before anything
    // can listen to CTRIPH.
    CMPSS_setDACValueHigh(CMPSS1_BASE, 4095U);
    CMPSS_configHighComparator(CMPSS1_BASE, CMPSS_INSRC_PIN);
    CMPSS_configOutputsHigh(CMPSS1_BASE, CMPSS_TRIPOUT_ASYNC_COMP |
                                        CMPSS_TRIP_ASYNC_COMP);
    CMPSS_enableModule(CMPSS1_BASE);
    dac_mux_found = dac_find_cmpss_mux();
    if (dac_mux_found == 0xFFU)
    {
        // Degraded: keep CTRIPH high-impedance to the TZ path - the max
        // threshold stays, so a floating mux input can never trip.
        CMPSS_setDACValueHigh(CMPSS1_BASE, 4095U);
    }
    else
    {
        CMPSS_setDACValueHigh(CMPSS1_BASE, CMPSS_THRESH);
    }

    // CMPSS1 CTRIPH -> EPWM X-BAR TRIP4 -> EPWM1 DCB **CBC** trip. CBC
    // (cycle-by-cycle) self-clears every PWM period, so EPWM1 keeps running
    // with a notch per CMPSS-high event and the flag is countable; the
    // first board run used the one-shot DCBEVT1 path, whose latch fired on
    // pre-config noise and permanently pinned EPWM1A low - which silently
    // starved the ECAP1 pad loopback (diag21, 2026-10-25).
    if (dac_mux_found != 0xFFU)
    {
        XBAR_setEPWMMuxConfig(BOARD_TRIP_CMPSS1, XBAR_EPWM_MUX00_CMPSS1_CTRIPH);
        XBAR_enableEPWMMux(BOARD_TRIP_CMPSS1, XBAR_MUX00);

        EPWM_selectDigitalCompareTripInput(EPWM1_BASE,
                                           EPWM_DC_COMBINATIONAL_TRIPIN4,
                                           EPWM_DC_TYPE_DCBH);
        EPWM_enableDigitalCompareTripCombinationInput(EPWM1_BASE,
                                                      EPWM_DC_COMBINATIONAL_TRIPIN4,
                                                      EPWM_DC_TYPE_DCBH);
        EPWM_setTripZoneDigitalCompareEventCondition(EPWM1_BASE,
                                                     EPWM_TZ_DC_OUTPUT_B2,
                                                     EPWM_TZ_EVENT_DCXH_HIGH);
        EPWM_enableTripZoneSignals(EPWM1_BASE, EPWM_TZ_SIGNAL_DCBEVT2);
        EPWM_setDigitalCompareEventSource(EPWM1_BASE, EPWM_DC_MODULE_B,
                                          EPWM_DC_EVENT_2,
                                          EPWM_DC_EVENT_SOURCE_ORIG_SIGNAL);
        EPWM_setTripZoneAction(EPWM1_BASE, EPWM_TZ_ACTION_EVENT_TZA,
                               EPWM_TZ_ACTION_LOW);
        EPWM_setTripZoneAction(EPWM1_BASE, EPWM_TZ_ACTION_EVENT_TZB,
                               EPWM_TZ_ACTION_LOW);
        // Whatever latched during the configuration window must not stick.
        EPWM_clearTripZoneFlag(EPWM1_BASE, EPWM_TZ_FLAG_OST |
                                          EPWM_TZ_FLAG_CBC);
    }

    dac_step_index = 0U;
    return (dac_lb_ch_a != 0xFFU) && (dac_lb_ch_b != 0xFFU);
}

void dac_cmpss_step(void)
{
    uint16_t sample;
    uint16_t expect;
    bool step_ok = true;

    // 1) DACA staircase -> ADCA loopback readback.
    DAC_setShadowValue(DACA_BASE, DAC_STEPCODE(dac_step_index));
    if (dac_bound && adc_scan_get_dac_a_sample(&sample))
    {
        expect = DAC_STEPCODE((dac_step_index == 0U) ? (DAC_STEPS - 1U)
                                                     : (dac_step_index - 1U));
        if (((sample > expect) ? (sample - expect) : (expect - sample)) >
            DAC_LB_TOL)
        {
            step_ok = false;
        }
    }
    // 2) DACB fixed level -> ADCC loopback readback.
    if (dac_bound && adc_scan_get_dac_b_sample(&sample))
    {
        if (((sample > 2048U) ? (sample - 2048U) : (2048U - sample)) >
            DAC_LB_TOL)
        {
            step_ok = false;
        }
    }
    if (step_ok)
    {
        g_dac_steps_ok++;
    }
    else
    {
        g_dac_steps_err++;
    }

    // 3) CMPSS high-status activity + EPWM1 CBC trip counting.
    if (dac_mux_found != 0xFFU)
    {
        if ((CMPSS_getStatus(CMPSS1_BASE) & CMPSS_STS_HI_FILTOUT) != 0U)
        {
            g_cmpss_high_hits++;
        }
        if ((EPWM_getCycleByCycleTripZoneFlagStatus(EPWM1_BASE) &
             EPWM_TZ_FLAG_CBC) != 0U)
        {
            g_tz_hits++;
            EPWM_clearCycleByCycleTripZoneFlag(EPWM1_BASE, EPWM_TZ_FLAG_CBC);
        }
    }

    dac_step_index++;
    if (dac_step_index >= DAC_STEPS)
    {
        dac_step_index = 0U;
    }
}

uint16_t dac_cmpss_mux(void)
{
    return dac_mux_found;
}
