//#############################################################################
//
// FILE:   svpwm.h
//
// TITLE:  Space-vector PWM modulator (PURE LOGIC, host-testable).
//
// DESCRIPTION:
//   min-max centred SVPWM: alpha/beta -> three per-mille phase duties.
//   No hardware, no globals - compiles and runs on the host
//   (see harness/tests/host/test_svpwm.c).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_SVPWM_H
#define MOTORPWM_SVPWM_H

#include <stdint.h>

//! Full-scale duty in per-mille (0 = always low, 1000 = always high).
#define SVPWM_DUTY_FULL_SCALE           1000U

//! Zero-vector (50% both directions) duty.
#define SVPWM_DUTY_MID                  500U

//! Error codes returned by svpwm_modulate().
#define SVPWM_OK                        0
#define SVPWM_ERR_NULL                  (-1)
#define SVPWM_ERR_MAG                   (-2)

typedef struct
{
    uint16_t a;      //!< phase A duty, per-mille 0..1000
    uint16_t b;      //!< phase B duty, per-mille 0..1000
    uint16_t c;      //!< phase C duty, per-mille 0..1000
} svpwm_duties_t;

//! Modulate one vector.
//! \param angle_x10    electrical angle in tenths of a degree (wraps mod 3600)
//! \param mag_permille magnitude in per-mille of full scale (0..1000)
//! \param out          receives the three phase duties (per-mille)
//! \return SVPWM_OK, SVPWM_ERR_NULL or SVPWM_ERR_MAG
int svpwm_modulate(uint16_t angle_x10, uint16_t mag_permille,
                   svpwm_duties_t *out);

#endif // MOTORPWM_SVPWM_H
