//#############################################################################
//
// FILE:   fsitx_loop_hw.h
//
// TITLE:  FSI TX->RX internal loopback demo (hardware layer, REQ-015).
//
// DESCRIPTION:
//   Primary evidence: FSI_RX internal loopback (FSI_RX_MAIN_CTRL_INT_LOOPBACK)
//   - the TX core feeds the RX core without leaving the chip. 8-word
//   frame_gen payloads, tag-matched, verified per frame. Optional physical
//   layer: jumper wires GPIO45<->GPIO3 / GPIO51<->GPIO39 (registered in
//   board_pins.h), not required by the gate.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_FSI_LOOP_HW_H
#define FULLPLATFORM_FSI_LOOP_HW_H

#include <stdint.h>
#include <stdbool.h>

bool fsi_loop_init(void);

//! One TX-start per call; completion verified on the next step (100 ms).
void fsi_loop_step(void);

extern volatile uint32_t g_fsi_ok;
extern volatile uint32_t g_fsi_err;

#endif // FULLPLATFORM_FSI_LOOP_HW_H
