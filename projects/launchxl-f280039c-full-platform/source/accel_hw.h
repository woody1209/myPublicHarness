//#############################################################################
//
// FILE:   accel_hw.h
//
// TITLE:  Accelerator matrix: BGCRC / AES / TMU / VCRC (REQ-005) + EPG (019).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_ACCEL_HW_H
#define FULLPLATFORM_ACCEL_HW_H

#include <stdint.h>
#include <stdbool.h>

bool accel_init(void);
void accel_step(void);           /* 1 s task: BGCRC window + AES roundtrip */

extern volatile uint32_t g_bgcrc_ok;      /* windows with stable golden CRC */
extern volatile uint32_t g_bgcrc_err;
extern volatile uint32_t g_aes_ok;        /* encrypt->decrypt roundtrips */
extern volatile uint32_t g_aes_err;
extern volatile uint32_t g_tmu_ok;        /* TMU vs math_ref within band */
extern volatile uint32_t g_tmu_err;
extern volatile uint32_t g_vcrc_ok;
extern volatile uint32_t g_vcrc_err;
extern volatile uint32_t g_epg_checked;   /* EPG register-level proof */

#endif
