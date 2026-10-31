//#############################################################################
//
// FILE:   self_test.h
//
// TITLE:  Power-on self test, pure-logic part (runs on target AND on host).
//
// DESCRIPTION:
//   The same reference vectors that the host unit tests assert are checked
//   here at run time, so a firmware image can prove its own core logic. The
//   module deliberately touches no hardware: it only exercises angle_gen,
//   svpwm and fmt, which lets the host test suite run it too
//   (harness/tests/host/test_mp_selftest.c).
//
//   Board-level checks (ePWM register readback, CMPSS bidirectional probe,
//   trip-zone arming) live in main.c: they need driverlib and cannot run on
//   the host.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_SELF_TEST_H
#define MOTORPWM_SELF_TEST_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    SELF_TEST_OK                   = 0U,
    SELF_TEST_FAIL_SVPWM_QUAD      = 1U,   //!< quadrant reference mismatch
    SELF_TEST_FAIL_SVPWM_GUARD     = 2U,   //!< over-modulation guard mismatch
    SELF_TEST_FAIL_SVPWM_ZERO      = 3U,   //!< zero vector not centred
    SELF_TEST_FAIL_SVPWM_ARGS      = 4U,   //!< illegal arguments not rejected
    SELF_TEST_FAIL_ANGLE_STEP      = 5U,   //!< rate stepping mismatch
    SELF_TEST_FAIL_ANGLE_WRAP      = 6U,   //!< wrap-around mismatch
    SELF_TEST_FAIL_ANGLE_ARGS      = 7U,   //!< illegal arguments not rejected
    SELF_TEST_FAIL_FMT_DEC         = 8U,   //!< decimal formatting mismatch
    SELF_TEST_FAIL_FMT_HEX         = 9U,   //!< hexadecimal formatting mismatch
    SELF_TEST_FAIL_FMT_BOUND       = 10U   //!< buffer bound handling mismatch
} self_test_result_t;

//! Run every pure-logic check. \return SELF_TEST_OK when all checks pass.
self_test_result_t self_test_run(void);

//! Last result, kept in a global so a debugger can read it without a console
//! (HIL leg H3 reads g_self_test_code == 0).
extern volatile uint32_t g_self_test_code;

#endif // MOTORPWM_SELF_TEST_H
