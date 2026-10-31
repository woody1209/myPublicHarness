//#############################################################################
//
// FILE:   pwm_bank_hw.h
//
// TITLE:  ePWM bank: all 8 modules / 16 channels (hardware layer, REQ-008).
//
// DESCRIPTION:
//   Frequency groups (SYSCLK 120 MHz, up-count unless noted):
//     EPWM1  10 kHz  50 %  -> GPIO0   (eCAP1 pad loopback; TZ target)
//     EPWM2  20 kHz  50 %  -> GPIO2   (eCAP2 pad loopback)
//     EPWM3  50 kHz  50 %  -> GPIO4   (eCAP3/HRCAP pad loopback; HRPWM MEP)
//     EPWM4   1 kHz  A/B 90-deg quadrature -> GPIO6/7  (eQEP1 source)
//     EPWM5  10 kHz  25 %  (no pin - register-level evidence)
//     EPWM6  10 kHz  50 %  (no pin)
//     EPWM7  10 kHz  75 %  (no pin)
//     EPWM8   5 kHz  A/B 90-deg quadrature -> GPIO14/15 (eQEP2 source)
//   Every configuration field is read back; a mismatch raises a health error.
//   TZ: CMPSS1 CTRIPH -> TRIP4 -> EPWM1 DCB one-shot (wired in dac_cmpss_hw).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_PWM_BANK_HW_H
#define FULLPLATFORM_PWM_BANK_HW_H

#include <stdint.h>
#include <stdbool.h>

//! Configure all 8 ePWM modules + pins + HRPWM + AQ/DB; read back every
//! period/compare register. \return true when all readbacks match.
bool pwm_bank_init(void);

//! 1 s task: re-read the period/compare registers of all modules and the
//! HRPWM MEP register; count mismatches into the health error counters.
void pwm_bank_verify(void);

//! Diagnostics for the health line / DSS: per-module config OK flag and
//! verification error count.
extern volatile uint32_t g_pwm_verify_err;
extern volatile uint32_t g_pwm_verifies;

#endif // FULLPLATFORM_PWM_BANK_HW_H
