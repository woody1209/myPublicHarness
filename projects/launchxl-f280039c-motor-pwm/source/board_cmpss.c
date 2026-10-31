//#############################################################################
//
// FILE:   board_cmpss.c
//
// TITLE:  CMPSS1 over-current trip chain (hardware layer).
//
// DESCRIPTION:
//   Wiring (TI cmpss_ex1_asynch.c template, F28003x driverlib):
//
//     CMPSS1 CTRIPH --X-BAR TRIP4 (mux00)--> ePWM1/2/3 DCB trip input 4
//       --> digital compare B-high event 1 --> DCBEVT1 (one-shot)
//       --> TZA/TZB action LOW (both outputs of all three modules)
//
//   Positive input = CMPIN1P: the CMPHPMUX reset value already selects the
//   dedicated analog pin shared with ADCA channel 2 (A2), so no ASysCtl mux
//   call is needed. Negative input = the internal reference DAC (shadow
//   sourced, software load on shadow write).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_cmpss.h"
#include "board_pins.h"
#include "board_clock.h"
#include "driverlib.h"

//! Analog settle after a threshold change before COMPSTS is meaningful:
//! comparator + DAC output stage. ~100 us at the verified SYSCLK (same scale
//! as the full-platform DAC settle, proven on target).
#define BOARD_CMPSS_SETTLE_LOOPS    ((uint32_t)(BOARD_SYSCLK_FREQ_HZ / 10000U))

static uint16_t board_cmpss_dac_value;

void board_cmpss_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CMPSS1);

    //
    // Order matters (full-platform diag21 lesson): park the reference DAC at
    // its maximum BEFORE the comparator is enabled, so neither a floating A2
    // nor configuration noise can assert CTRIPH while the X-BAR/TZ wiring is
    // still being brought up.
    //
    // config = CMPSS_DACSRC_SHDW (0): DAC value source = shadow register,
    // SWLOADSEL bit7 = 0 = CMPSS_DACVAL_SYSCLK (load shadow->active on every
    // SYSCLK tick). Passing only the source is correct - the SYSCLK load
    // selector IS the 0 value of SWLOADSEL (hw_cmpss.h: SWLOADSEL=0x80,
    // CMPSS_DACVAL_SYSCLK=0x0000, CMPSS_DACVAL_PWMSYNC=0x0080). Do NOT OR
    // CMPSS_DACVAL_SYSCLK into config - that would set SWLOADSEL=1 and switch
    // the load to PWMSYNC (wrong). This was the misread that made the probe
    // lo=1 flaky (diag_dac.js 2026-10-25: the original code was correct).
    //
    CMPSS_configDAC(CMPSS1_BASE, CMPSS_DACSRC_SHDW);
    CMPSS_setDACValueHigh(CMPSS1_BASE, BOARD_CMPSS_DAC_MAX);

    //
    // Positive input = CMPIN1P (A2, reset-default mux), negative input =
    // internal reference DAC. Asynchronous comparator output feeds both the
    // status path and CTRIPH.
    //
    CMPSS_configHighComparator(CMPSS1_BASE, CMPSS_INSRC_DAC);
    CMPSS_configOutputsHigh(CMPSS1_BASE, CMPSS_TRIPOUT_ASYNC_COMP |
                                        CMPSS_TRIP_ASYNC_COMP);
    CMPSS_enableModule(CMPSS1_BASE);

    board_cmpss_dac_value = BOARD_CMPSS_DAC_MAX;
}

void board_cmpss_arm_tz(void)
{
    uint32_t i;

    static const uint32_t epwm_base[3] = { EPWM1_BASE, EPWM2_BASE, EPWM3_BASE };

    //
    // X-BAR: CMPSS1 CTRIPH -> TRIP4, mux00 (board_pins.h truth).
    //
    XBAR_setEPWMMuxConfig(BOARD_TRIP_CMPSS1, XBAR_EPWM_MUX00_CMPSS1_CTRIPH);
    XBAR_enableEPWMMux(BOARD_TRIP_CMPSS1, XBAR_MUX00);

    //
    // Per-module digital compare chain, in the exact order of the TI
    // cmpss_ex1_asynch.c template.
    //
    for (i = 0U; i < 3U; i++)
    {
        EPWM_setTripZoneDigitalCompareEventCondition(epwm_base[i],
                                                     EPWM_TZ_DC_OUTPUT_B1,
                                                     EPWM_TZ_EVENT_DCXH_HIGH);
        EPWM_enableDigitalCompareTripCombinationInput(epwm_base[i],
                                                      EPWM_DC_COMBINATIONAL_TRIPIN4,
                                                      EPWM_DC_TYPE_DCBH);
        EPWM_enableTripZoneSignals(epwm_base[i], EPWM_TZ_SIGNAL_DCBEVT1);
        EPWM_setDigitalCompareEventSource(epwm_base[i], EPWM_DC_MODULE_B,
                                          EPWM_DC_EVENT_1,
                                          EPWM_DC_EVENT_SOURCE_ORIG_SIGNAL);
    }

    //
    // Whatever latched during the configuration window must not stick: the
    // one-shot event is armed, so clear the flags AFTER enabling.
    //
    for (i = 0U; i < 3U; i++)
    {
        EPWM_clearTripZoneFlag(epwm_base[i], EPWM_TZ_INTERRUPT |
                                          EPWM_TZ_FLAG_DCBEVT1);
    }
}

void board_cmpss_set_dac(uint16_t value)
{
    uint16_t clamped = (value > BOARD_CMPSS_DAC_MAX) ? BOARD_CMPSS_DAC_MAX
                                                     : value;

    CMPSS_setDACValueHigh(CMPSS1_BASE, clamped);
    board_cmpss_dac_value = clamped;
}

uint16_t board_cmpss_get_dac(void)
{
    return board_cmpss_dac_value;
}

bool board_cmpss_comp_status(void)
{
    //
    // The synchronous/filtered status bit tracks the comparator after the
    // settle delay; the caller must provide that delay.
    //
    return ((CMPSS_getStatus(CMPSS1_BASE) & CMPSS_STS_HI_FILTOUT) != 0U);
}

void board_cmpss_clear_filter_latch(void)
{
    //
    // COMPSTS bit1 (COMPHLATCH) is sticky: once the digital filter sees a
    // trip it stays set until software clears it (CMPSS_O_COMPSTSCLR bit1).
    // Clearing it between probe directions keeps each direction's read
    // independent (diag_dac.js 2026-10-25).
    //
    CMPSS_clearFilterLatchHigh(CMPSS1_BASE);
}

bool board_cmpss_wait_status(bool expect, uint32_t timeout_ms)
{
    //
    // 1 ms granularity: board_delay_ms is the calibrated busy wait shared
    // with the rest of the firmware (board_clock.c), and the comparator
    // answers within one SYSCLK of the DAC shadow load anyway.
    //
    while (timeout_ms > 0U)
    {
        if (board_cmpss_comp_status() == expect)
        {
            return true;
        }
        board_delay_ms(1U);
        timeout_ms--;
    }
    return (board_cmpss_comp_status() == expect);
}

bool board_cmpss_tz_latched(void)
{
    static const uint32_t epwm_base[3] = { EPWM1_BASE, EPWM2_BASE, EPWM3_BASE };
    uint32_t i;
    bool latched = false;

    for (i = 0U; i < 3U; i++)
    {
        if ((EPWM_getTripZoneFlagStatus(epwm_base[i]) &
             EPWM_TZ_FLAG_DCBEVT1) != 0U)
        {
            latched = true;
        }
    }
    return latched;
}

void board_cmpss_clear_tz(void)
{
    static const uint32_t epwm_base[3] = { EPWM1_BASE, EPWM2_BASE, EPWM3_BASE };
    uint32_t i;

    for (i = 0U; i < 3U; i++)
    {
        EPWM_clearTripZoneFlag(epwm_base[i], EPWM_TZ_INTERRUPT |
                                          EPWM_TZ_FLAG_DCBEVT1);
    }
}
