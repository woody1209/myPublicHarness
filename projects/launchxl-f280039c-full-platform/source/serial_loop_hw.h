//#############################################################################
//
// FILE:   serial_loop_hw.h
//
// TITLE:  SCIB + LINA/LINB internal-loopback serial demo (REQ-013).
//
// DESCRIPTION:
//   SCIB runs SCI_enableLoopback; LINA/LINB run in SCI-compatible mode with
//   LIN digital loopback. Each 10 ms step services one module's fixed
//   frame_gen byte exchange; byte-exact compares feed the m3 status bits.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_SERIAL_LOOP_HW_H
#define FULLPLATFORM_SERIAL_LOOP_HW_H

#include <stdint.h>
#include <stdbool.h>

bool serial_loop_init(void);

//! Round-robin one exchange step per call (10 ms task).
void serial_loop_step(void);

extern volatile uint32_t g_ser_ok;
extern volatile uint32_t g_ser_err;

#endif // FULLPLATFORM_SERIAL_LOOP_HW_H
