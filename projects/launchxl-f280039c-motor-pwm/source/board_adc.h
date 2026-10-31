//#############################################################################
//
// FILE:   board_adc.h
//
// TITLE:  ADCA single-channel sampling front end (hardware layer).
//
// DESCRIPTION:
//   One SOC on ADCA, triggered by ePWM1 SOCA, converting the ISENSE analog
//   pin A2 (shared with the CMPSS1 positive input). The superloop polls the
//   ADCINT1 end-of-conversion flag - no interrupt is ever enabled, so nothing
//   can preempt the control loop.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_BOARD_ADC_H
#define MOTORPWM_BOARD_ADC_H

#include <stdint.h>

//! Configure ADCA SOC0 <- ePWM1 SOCA, channel = A2 (BOARD_ISENSE_ADC_CHANNEL).
//! Call after board_epwm_init() (the trigger source must exist first) and
//! after board_clock_init() (the power-up delay assumes the verified SYSCLK).
void board_adc_init(void);

//! \return the most recent conversion result (0..4095). When no new
//!         conversion completed since the last call the previous value is
//!         returned (first call before any conversion returns 0).
uint16_t board_adc_read_latest(void);

//! Force one conversion and wait for it (bounded). Used by the self test and
//! by HIL experiments that need a sample on demand.
//! \return the raw conversion result (0..4095)
uint16_t board_adc_read_raw_blocking(void);

#endif // MOTORPWM_BOARD_ADC_H
