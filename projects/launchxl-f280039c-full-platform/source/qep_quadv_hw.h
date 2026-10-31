//#############################################################################
//
// FILE:   qep_quadv_hw.h
//
// TITLE:  eQEP 1/2 quadrature pad-loopback (hardware layer, REQ-010).
//
// DESCRIPTION:
//   EPWM4A/B (1 kHz, 90 deg) and EPWM8A/B (5 kHz, 90 deg) loop into
//   eQEP1/eQEP2 through Input X-BAR 1-4 and the EPWM X-BAR trips
//   (TI eqep_ex3 source mapping). Quadrature decode counts 4 edges per
//   PWM cycle; the 100 ms task samples QPOSCNT deltas and the direction
//   flag, judging speed against the theoretical value (+/-10 %).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_QEP_QUADV_HW_H
#define FULLPLATFORM_QEP_QUADV_HW_H

#include <stdint.h>
#include <stdbool.h>

//! Latest position counter readings (free-running).
extern volatile int32_t g_qep_pos[2];
//! Theoretical counts per second (4 edges x PWM frequency).
extern const uint32_t g_qep_rate_hz[2];
//! Last measured counts/s per channel (0 = not yet measured).
extern volatile uint32_t g_qep_rate_meas[2];
//! Direction flag of the last sample: true = forward (A leads B).
extern volatile bool g_qep_forward[2];
//! Channel validity flags.
extern volatile uint32_t g_qep_ok_mask;

bool qep_quadv_init(void);
void qep_quadv_step(void);

#endif // FULLPLATFORM_QEP_QUADV_HW_H
