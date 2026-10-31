//#############################################################################
//
// FILE:   board_adc.c
//
// TITLE:  ADCA single-channel sampling front end (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_adc.h"
#include "board_pins.h"
#include "board_clock.h"
#include "driverlib.h"

//! Sample window in SYSCLK cycles (TRM minimum is 14 for 12-bit; 75 leaves
//! margin for the source impedance of a floating analog pin).
#define BOARD_ADC_ACQPS            75U

//! Power-up settle before the first conversion: 5 ms (SysCtl_delay costs
//! ~5 cycles per count, so SYSCLK/1000 counts = 5 ms). TRM minimum is 1 ms
//! after ADCPWDNZ; 5 ms is plain margin. (2026-10-25 note: the boot probe
//! failure that motivated this was NOT a power-up race - the root cause was
//! the missing ADC_enableInterrupt() below - but the margin is harmless.)
#define BOARD_ADC_POWERUP_LOOPS    ((uint32_t)(BOARD_SYSCLK_FREQ_HZ / 1000U))

//! Bounded wait per read attempt. One conversion at 75 acqps on a
//! 30 MHz ADCCLK takes ~3 us; the generous limit only guards against a
//! mis-wired trigger, it is never reached in normal operation.
#define BOARD_ADC_WAIT_LIMIT       200000U

//! Read attempts before giving up (REQ-003): a cold ADC can still drop the
//! first forced SOC while its power-up sequence runs, so a retry with a
//! ~2.5 ms pause keeps the probe honest on the very first call.
#define BOARD_ADC_READ_ATTEMPTS    4U

static uint16_t board_adc_latest;

void board_adc_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCA);

    ADC_setVREF(ADCA_BASE, ADC_REFERENCE_INTERNAL, ADC_REFERENCE_3_3V);
    ADC_setPrescaler(ADCA_BASE, ADC_CLK_DIV_4_0);
    ADC_setInterruptPulseMode(ADCA_BASE, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(ADCA_BASE);
    SysCtl_delay(BOARD_ADC_POWERUP_LOOPS);

    //
    // SOC0: hardware trigger from ePWM1 SOCA, channel A2. The ePWM event
    // restarts the conversion every carrier period, so the superloop always
    // finds a fresh sample without ever blocking the loop for the ADC.
    //
    ADC_setupSOC(ADCA_BASE, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM1_SOCA,
                 BOARD_ISENSE_ADC_CHANNEL, BOARD_ADC_ACQPS);

    //
    // The trigger lives on the ePWM side: pulse SOCA once per carrier period
    // (TBCTR = PRD). ePWM1 is already configured by board_epwm_init(); the
    // ADC-related trigger enabling is kept here so the whole sampling chain
    // is visible in one module.
    //
    EPWM_setADCTriggerSource(EPWM1_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD);
    EPWM_enableADCTrigger(EPWM1_BASE, EPWM_SOC_A);

    //
    // SOC event prescaler - THE load-bearing line of this whole chain
    // (root cause of the "ADC never triggers" bug, proven on target
    // 2026-10-25 with diag_etps2.js):
    //   ETSEL only SELECTS the event source (SOCASEL) and enables the
    //   output (SOCAEN). The SOCA pulse itself is additionally gated by
    //   the event prescaler, whose reset default is DISABLED:
    //     ETPS.SOCAPRD   = 00 (legacy 2-bit path, selected when
    //                            ETPS.SOCPSSEL = 0, the reset default)
    //     ETSOCPS.SOCAPRD2 = 0000 (extended 4-bit path)
    //   TRM (SPRUHE8E, same ePWM as F28003x): "00 - Disable the SOCA
    //   event counter. No EPWMxSOCA pulse will be generated."
    //   With SOCAEN=1 and SOCASEL=PRD but the prescaler disabled,
    //   ETFLG.SOCA never asserted and the ADC never converted - the
    //   superloop read 0xFFFF forever. Writing ETSOCPS alone does NOT
    //   help: ETSOCPS is only the selected source when SOCPSSEL = 1.
    //   EPWM_setADCTriggerEventPrescale(..., 1) sets SOCPSSEL = 1 and
    //   SOCAPRD2 = 1 = "pulse on FIRST event" (TI adc_ex1_soc_epwm.c
    //   uses the equivalent ETPS.SOCAPRD = 1 on the legacy path; both
    //   were verified on target).
    //
    EPWM_setADCTriggerEventPrescale(EPWM1_BASE, EPWM_SOC_A, 1U);

    //
    // EOC0 is the ADCINT1 source. The flag is polled (never enabled as a PIE
    // interrupt), so an ADC fault can never vector into the boot ROM ITRAP
    // handler and reset the device.
    //
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);

    //
    // INT1E (ADCINTSEL1N2 bit 5) is NOT the PIE interrupt - it is the gate on
    // the ADCINTFLG bit itself. Proven on target 2026-10-25 (diag_int1e.js):
    // with INT1E clear a forced SOC converts (RESULT0 updates) but the flag
    // never asserts, so every EOC poll times out and board_adc_read_raw_
    // blocking() returned 0xFFFF - that was the "cmpss: adc=65535" boot
    // failure. Setting INT1E made the flag assert within one conversion.
    //
    // Enabling it here is safe for the polling design: the PIE-level enable
    // (IER bit for ADCINT1) and the global mask (INTM) both stay off - the
    // firmware never calls EINT - so the pulse only ever latches in the PIE
    // IFR and no ISR can vector.
    //
    ADC_enableInterrupt(ADCA_BASE, ADC_INT_NUMBER1);

    board_adc_latest = 0U;
}

uint16_t board_adc_read_latest(void)
{
    if (ADC_getInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1) != false)
    {
        ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
        board_adc_latest = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
    }
    return board_adc_latest;
}

uint16_t board_adc_read_raw_blocking(void)
{
    uint32_t attempt;
    uint32_t guard;

    //
    // Clear the flag FIRST, force, then wait: whatever sets the flag
    // afterwards belongs to a conversion that started after the clear. The
    // ePWM SOCA path also keeps setting ADCINT1 every carrier period, so a
    // flag seen before the clear could carry an older sample. (The original
    // "stale baseline" theory for the boot failure was wrong - the flag was
    // never asserting at all because INT1E was clear - but clear-then-force
    // is still the correct discipline, so it stays.)
    //
    for (attempt = 0U; attempt < BOARD_ADC_READ_ATTEMPTS; attempt++)
    {
        ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
        ADC_forceSOC(ADCA_BASE, ADC_SOC_NUMBER0);

        guard = BOARD_ADC_WAIT_LIMIT;
        while ((ADC_getInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1) == false) &&
               (guard > 0U))
        {
            guard--;
        }

        if (guard > 0U)
        {
            ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
            board_adc_latest = ADC_readResult(ADCARESULT_BASE,
                                              ADC_SOC_NUMBER0);
            return board_adc_latest;
        }

        //
        // Converter still powering up: the forced SOC was dropped. Pause
        // (~2.5 ms) and retry - the next force lands once the ADC is ready.
        //
        SysCtl_delay(BOARD_SYSCLK_FREQ_HZ / 2000U);
    }

    //
    // Timeout placeholder (REQ-003): never block the caller forever. The
    // superloop path (board_adc_read_latest) is non-blocking by design and
    // is unaffected.
    //
    return 0xFFFFU;
}
