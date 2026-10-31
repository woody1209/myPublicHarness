//#############################################################################
//
// FILE:   cap_meas_hw.h
//
// TITLE:  eCAP 1-3 pad-loopback frequency/duty measurement (REQ-009).
//
// DESCRIPTION:
//   ECAP1/2/3 continuously capture the pad-loopback PWM from EPWM1A/2A/3A
//   (Input X-BAR 7/8/9). The 10 ms task computes the last rising-to-rising
//   period and duty from CAP1/CAP2/CAP3 (wrap-safe), averages them, and
//   judges each channel against its configured frequency (same SYSCLK on
//   both sides, so the ratio is exact). ECAP3 additionally runs in high-
//   resolution mode (HRCAP); when HR calibration is unavailable the channel
//   degrades to standard mode with the result logged.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_CAP_MEAS_HW_H
#define FULLPLATFORM_CAP_MEAS_HW_H

#include <stdint.h>
#include <stdbool.h>

//! Measured mean frequencies in Hz (0 = no valid capture yet).
extern volatile uint32_t g_cap_hz[3];
//! Configured frequencies for the judgement (Hz).
extern const uint32_t g_cap_cfg_hz[3];
//! Samples accumulated per channel.
extern volatile uint32_t g_cap_samples[3];
//! Channel validity flags (all 3 = REQ-009 PASS band satisfied).
extern volatile uint32_t g_cap_ok_mask;
//! True when ECAP3 runs in high-resolution mode (else degraded, logged).
extern volatile bool g_cap_hr_active;

bool cap_meas_init(void);
void cap_meas_step(void);

#endif // FULLPLATFORM_CAP_MEAS_HW_H
