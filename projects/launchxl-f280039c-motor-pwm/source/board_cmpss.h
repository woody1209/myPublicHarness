//#############################################################################
//
// FILE:   board_cmpss.h
//
// TITLE:  CMPSS1 over-current trip chain (hardware layer).
//
// DESCRIPTION:
//   CMPSS1 high comparator: positive input = CMPIN1P (the A2 analog pin,
//   shared with the ADC), negative input = the internal reference DAC.
//   CTRIPH is routed through the ePWM X-BAR (TRIP4, mux00) into the digital
//   compare B-high path of ePWM1/2/3, where DCBEVT1 (ONE-SHOT) forces both
//   outputs of all three modules low until software clears the latch.
//
//   The wiring is only armed AFTER the self test has proven the comparator
//   responds in both directions (REQ-004): the full-platform build once
//   latched a spurious trip because the reference DAC was still at its reset
//   threshold while the X-BAR mux floated (diag21, 2026-10-25).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_BOARD_CMPSS_H
#define MOTORPWM_BOARD_CMPSS_H

#include <stdint.h>
#include <stdbool.h>

//! Maximum reference DAC code (12-bit).
#define BOARD_CMPSS_DAC_MAX            4095U

//! Bounded wait deadline for the comparator to follow a reference change.
//! The DAC shadow loads every SYSCLK (COMPDACCTL.SWLOADSEL = 0 = CMPSS_DACVAL_SYSCLK,
//! the reset default; board_cmpss_init keeps it) and the comparator is
//! asynchronous, but the digital filter + DAC output stage need ~100 us to
//! settle (verified scale). 200 ms gives 2000x margin and is still bounded -
//! a broken chain fails with a verdict instead of a blind fixed delay that
//! may sample too early (or too late).
#define BOARD_CMPSS_SETTLE_TIMEOUT_MS  200U

//! Configure CMPSS1 with the reference DAC parked at BOARD_CMPSS_DAC_MAX and
//! the X-BAR/TZ wiring still disarmed. Call after board_epwm_init().
void board_cmpss_init(void);

//! Wire CTRIPH -> X-BAR TRIP4 -> DCBH -> DCBEVT1 (one-shot) on ePWM1/2/3.
//! MUST only be called after the CMPSS bidirectional self test passed.
void board_cmpss_arm_tz(void);

//! Set the reference threshold (0..4095 codes). Values above the DAC maximum
//! are clamped.
void board_cmpss_set_dac(uint16_t value);

//! \return the current reference threshold code.
uint16_t board_cmpss_get_dac(void);

//! \return true when the high comparator output is active (input above the
//!         reference), i.e. the over-current condition is present.
bool board_cmpss_comp_status(void);

//! Clear the high-comparator digital-filter latch (COMPSTS bit1) so a fresh
//! comparison can start from a clean state. Use between the two directions of
//! the CMPSS probe: the latch is sticky and would otherwise carry the first
//! direction's result into the second (diag_dac.js 2026-10-25: bit1 read 1
//! even after the comparator de-asserted).
void board_cmpss_clear_filter_latch(void);

//! Bounded wait for the high comparator status to reach \e expect (true =
//! tripped). Returns true on match within BOARD_CMPSS_SETTLE_TIMEOUT_MS,
//! false on timeout - the caller decides what a timeout means. Use this
//! instead of a blind delay after board_cmpss_set_dac(): the comparator is
//! asynchronous, so polling costs nothing when the chain is healthy and
//! still yields a deterministic verdict when it is not.
bool board_cmpss_wait_status(bool expect, uint32_t timeout_ms);

//! \return true when any of ePWM1/2/3 has the one-shot DCBEVT1 latched.
bool board_cmpss_tz_latched(void);

//! Clear the one-shot trip latch on ePWM1/2/3 (software recovery).
void board_cmpss_clear_tz(void);

#endif // MOTORPWM_BOARD_CMPSS_H
