//#############################################################################
//
// FILE:   i2c_pair_hw.h
//
// TITLE:  I2CA controller loopback demo (hardware layer, REQ-012).
//
// DESCRIPTION:
//   Primary evidence: I2CA in controller mode with I2C_enableLoopback -
//   the transmitted byte stream loops inside the module, so the controller
//   both master-writes and receives its own frame. 8 word-sized bytes from
//   frame_gen are written then read back and compared. The A<->B jumper
//   path (board_pins.h) stays a future physical-layer proof; failure to
//   find the loopback path degrades to SHOULD-severity health counting.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_I2C_PAIR_HW_H
#define FULLPLATFORM_I2C_PAIR_HW_H

#include <stdint.h>
#include <stdbool.h>

bool i2c_pair_init(void);

//! One loopback write+readback pass (100 ms task step, 2 passes/transaction).
void i2c_pair_step(void);

extern volatile uint32_t g_i2c_ok;
extern volatile uint32_t g_i2c_err;

#endif // FULLPLATFORM_I2C_PAIR_HW_H
