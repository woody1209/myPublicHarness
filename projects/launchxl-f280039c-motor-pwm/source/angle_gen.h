//#############################################################################
//
// FILE:   angle_gen.h
//
// TITLE:  Open-loop electrical angle generator (PURE LOGIC, host-testable).
//
// DESCRIPTION:
//   Fixed-point integrator producing the electrical angle in tenths of a
//   degree (0..3599). No hardware, no globals beyond the caller's state -
//   compiles and runs on the host (see harness/tests/host/test_angle_gen.c).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_ANGLE_GEN_H
#define MOTORPWM_ANGLE_GEN_H

#include <stdint.h>

//! Angle resolution: 0.1 degree. Full mechanical... no, electrical turn.
#define ANGLE_GEN_FULL_SCALE_X10        3600U

//! Rate unit: 0.01 Hz. 500 = 5.00 Hz electrical.
#define ANGLE_GEN_RATE_MAX_X100         50000U   //!< 500.00 Hz upper bound

typedef struct
{
    uint32_t angle_x10;      //!< current angle, 0..3599 (tenths of a degree)
    uint32_t rate_hz_x100;   //!< electrical frequency, 0.01 Hz units
    uint64_t accum;          //!< sub-step remainder (exact rational carry)
} angle_gen_t;

//! Initialise with a rate (0.01 Hz units). Rate 0 parks the angle.
void angle_gen_init(angle_gen_t *g, uint32_t rate_hz_x100);

//! Advance by tick_us microseconds. \return the new angle (0..3599).
//! tick_us == 0 or g == 0 returns the current angle unchanged.
uint32_t angle_gen_advance(angle_gen_t *g, uint32_t tick_us);

//! Set the rate (0.01 Hz units). \return 0 on success, -1 if out of range
//! (the previous rate is kept).
int angle_gen_set_rate_hz_x100(angle_gen_t *g, uint32_t rate_hz_x100);

//! \return the current angle without advancing.
uint32_t angle_gen_get(const angle_gen_t *g);

#endif // MOTORPWM_ANGLE_GEN_H
