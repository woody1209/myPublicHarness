//#############################################################################
//
// FILE:   dac_cmpss_hw.h
//
// TITLE:  DAC -> ADC loopback + CMPSS1/TZ demo (hardware layer, REQ-007).
//
// DESCRIPTION:
//   Primary evidence (zero external hardware):
//   - DACA staircase (8 levels) -> internal analog loopback -> ADCA readback
//     is compared against the programmed code each step.
//   - DACB fixed level -> ADCC readback likewise.
//   - CMPSS1: positive input mux is auto-discovered (sweep CMPHPMUX values,
//     the one whose high status tracks the DACA staircase wins); its internal
//     reference DAC forms the threshold. If no mux tracks the staircase the
//     requirement's documented downgrade applies (register-level only).
//   - CMPSS1 CTRIPH -> EPWM X-BAR TRIP4 -> EPWM1 DCB/TZ action: each CMPSS
//     trip forces EPWM1A/B low, counted via the TZ flag.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_DAC_CMPSS_HW_H
#define FULLPLATFORM_DAC_CMPSS_HW_H

#include <stdint.h>
#include <stdbool.h>

//! Number of staircase levels (codes 0..4095 spread over DAC_STEPS points).
#define DAC_STEPS                   8U

//! Init DACs, enable the internal DAC->ADC loopback, and run the channel
//! discovery (ADC scan for the loopback channel + CMPSS mux sweep).
//! Needs adc_scan_init() first. \return false only on a driver-level fault;
//! a missing loopback path degrades (status globals), it does not abort.
bool dac_cmpss_init(void);

//! Advance the DACA staircase one level (100 ms task): program the new code,
//! verify the ADCA loopback readback (may lag one cycle), verify the DACB
//! fixed level, and count CMPSS/TZ activity.
void dac_cmpss_step(void);

//! \return discovered CMPHPMUX value for CMPSS1 (0xFFFF = no loopback path).
uint16_t dac_cmpss_mux(void);

//! Diagnostics for the health line / DSS:
extern volatile uint32_t g_dac_steps_ok;
extern volatile uint32_t g_dac_steps_err;
extern volatile uint32_t g_cmpss_high_hits;
extern volatile uint32_t g_tz_hits;

#endif // FULLPLATFORM_DAC_CMPSS_HW_H
