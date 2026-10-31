//#############################################################################
//
// FILE:   can_loop_hw.h
//
// TITLE:  MCANA classic-CAN self-test loopback (hardware layer, REQ-014).
//
// DESCRIPTION:
//   MCANA runs the classic CAN driver (can.h) in TEST_LBACK loopback mode:
//   standard (11-bit id 1) and extended (29-bit id 0x1ABCDE) frames are
//   transmitted and received without any transceiver. One frame pair per
//   100 ms step; payload from frame_gen, verified byte-exact.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_CAN_LOOP_HW_H
#define FULLPLATFORM_CAN_LOOP_HW_H

#include <stdint.h>
#include <stdbool.h>

bool can_loop_init(void);

//! Alternate std/ext frame transmit+receive (100 ms task step).
void can_loop_step(void);

extern volatile uint32_t g_can_ok;
extern volatile uint32_t g_can_err;

#endif // FULLPLATFORM_CAN_LOOP_HW_H
