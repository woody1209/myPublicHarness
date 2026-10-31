//#############################################################################
//
// FILE:   dcc_mon_hw.h
//
// TITLE:  DCC0 continuous SYSCLK monitor (hardware layer, spec 0002 REQ-002).
//
// DESCRIPTION:
//   DCC0 verifies SYSCLK (PLL, counter 1) against INTOSC2 (10 MHz,
//   counter 0) in a 800 us window. PLL drift/divergence latches the DCC
//   error flag, which the 10 ms task polls into the health monitor.
//   PLL and INTOSC2 share the same source, so the ratio is exact by design;
//   the valid window only absorbs quantisation margin.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_DCC_MON_HW_H
#define FULLPLATFORM_DCC_MON_HW_H

#include <stdint.h>
#include <stdbool.h>

//! Configure DCC0 and start continuous monitoring. \return false when any
//! register readback disagrees (module wired wrong).
bool dcc_mon_init(void);

//! Poll the latched error flag (10 ms task). \return true when the last
//! window verified; false latches a health failure.
bool dcc_mon_check(void);

//! Diagnostics for the health line / DSS: total windows checked, errors, and
//! the CURRENT consecutive-error run (the REQ-002 judgement variable - H7
//! gates on consec==0, a recovered sporadic error is not a failure).
extern volatile uint32_t g_dcc_windows;
extern volatile uint32_t g_dcc_errors;
extern volatile uint32_t g_dcc_consec;

#endif // FULLPLATFORM_DCC_MON_HW_H
