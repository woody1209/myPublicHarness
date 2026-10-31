//#############################################################################
//
// FILE:   cla_app_shared.h
//
// TITLE:  CPU<->CLA shared contract (spec 0002 REQ-004).
//
// DESCRIPTION:
//   CPU writes 16 input words into CpuToCla1MsgRAM, forces CLA Task 1,
//   Task 1 computes a 16-tap moving-average + min/max over the inputs and
//   writes the results into CLA1ToCpuMsgRAM. The CPU judges against math_ref
//   results. Only integer arithmetic: the CLA has no C runtime library.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_CLA_APP_SHARED_H
#define FULLPLATFORM_CLA_APP_SHARED_H

#include <stdint.h>

#define CLA_N_IN                  16U

#if !defined(CLA_C)   /* CPU-side view */
extern volatile uint32_t cla_in[CLA_N_IN];       /* CpuToCla1MsgRAM  */
extern volatile uint32_t cla_sum;                /* CLA1ToCpuMsgRAM  */
extern volatile uint32_t cla_min;
extern volatile uint32_t cla_max;
extern volatile uint32_t cla_done;               /* task 1 writes 0xA5A5 */
#else                  /* CLA-side view (same symbols, msg RAM sections) */
extern uint32_t cla_in[CLA_N_IN];
extern uint32_t cla_sum;
extern uint32_t cla_min;
extern uint32_t cla_max;
extern uint32_t cla_done;
#endif

#endif
