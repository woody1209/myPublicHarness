//#############################################################################
//
// FILE:   self_test.h
//
// TITLE:  Power-on self test (pure logic - runs on target AND on host).
//
// DESCRIPTION:
//   The same reference constants that the host unit tests assert are checked
//   here at run time, so a firmware image can prove its own core logic. The
//   module deliberately touches no hardware: it only exercises led_pattern and
//   fmt, which lets the host test suite run it too.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef LEDCHASER_SELF_TEST_H
#define LEDCHASER_SELF_TEST_H

#include <stdint.h>

typedef enum
{
    SELF_TEST_OK               = 0U,
    SELF_TEST_FAIL_PATTERN_FNV = 1U,   //!< FNV-1a reference mismatch
    SELF_TEST_FAIL_PATTERN_SEQ = 2U,   //!< hard-coded per-step sequence mismatch
    SELF_TEST_FAIL_FMT_DEC     = 3U,   //!< decimal formatting mismatch
    SELF_TEST_FAIL_FMT_HEX     = 4U,   //!< hexadecimal formatting mismatch
    SELF_TEST_FAIL_FMT_BOUND   = 5U    //!< buffer bound handling mismatch
} self_test_result_t;

//! Run every check. \return SELF_TEST_OK when all checks pass.
self_test_result_t self_test_run(void);

//! Last result, kept in a global so a debugger can read it without a console.
extern volatile uint32_t g_self_test_code;

#endif // LEDCHASER_SELF_TEST_H
