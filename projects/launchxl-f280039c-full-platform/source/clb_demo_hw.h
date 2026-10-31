//#############################################################################
//
// FILE:   clb_demo_hw.h
//
// TITLE:  CLB tile register-interface demo (hardware layer, REQ-017).
//
// DESCRIPTION:
//   CLB1 tile with the global enable + GPREG control written and the tile's
//   CTR0 register loaded/read back through the SPI buffer interface
//   (CLB_writeInterface). HLC-programmed logic is the tool-flow stage and
//   is recorded as pending in spec 0002 REQ-017 (needs the CLB Tool).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_CLB_DEMO_HW_H
#define FULLPLATFORM_CLB_DEMO_HW_H

#include <stdint.h>
#include <stdbool.h>

bool clb_demo_init(void);
void clb_demo_step(void);        /* 1 s: CTR0 write/readback via buffer */

extern volatile uint32_t g_clb_ok;
extern volatile uint32_t g_clb_err;

#endif
