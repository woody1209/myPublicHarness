//#############################################################################
//
// FILE:   board_epwm.h
//
// TITLE:  ePWM1/2/3 three-phase complementary PWM with dead-band (hw layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_BOARD_EPWM_H
#define MOTORPWM_BOARD_EPWM_H

#include <stdint.h>
#include <stdbool.h>

//! Configure ePWM1/2/3: up-down 10 kHz, in-phase, complementary B outputs
//! (POLSEL=2), RED=FED=120 cycles dead-band, SVPWM action qualifier.
//! Call after board_clock_init(). Clock gates the modules itself.
void board_epwm_init(void);

//! Write the three phase duties (per-mille 0..1000) to CMPA of ePWM1/2/3.
void board_epwm_set_duties(uint16_t a_permille, uint16_t b_permille,
                           uint16_t c_permille);

//! Verify the configured registers match the declared constants.
//! \return true when TBPRD/DBRED/DBFED/DBCTL all read back as designed.
bool board_epwm_verify(void);

//! Read back the configuration for the console line
//! "pwm: prd=6000 db=120 pol=2".
void board_epwm_report(uint16_t *prd, uint16_t *db, uint16_t *pol);

#endif // MOTORPWM_BOARD_EPWM_H
