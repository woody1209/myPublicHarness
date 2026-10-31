//#############################################################################
//
// FILE:   test_fp_selftest.c
//
// TITLE:  Host execution of the full-platform extended self test (spec 0002).
//
// DESCRIPTION:
//   The same pure-logic self test that runs at power-on on the board
//   (self_test_run in projects/launchxl-f280039c-full-platform) is executed
//   here on the host: pattern/FNV references, fmt vectors, scheduler
//   invariants and health thresholds. No two truths (REQ-004/REQ-020).
//   This suite also covers the fmt and led_pattern module copies that the
//   full-platform project compiles for the target.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "self_test.h"

void fp_selftest_tests(void)
{
    fw_test_suite_begin("full-platform extended self test (on host)");

    fw_test_case_begin("self_test_run() == SELF_TEST_OK");
    FW_ASSERT_EQ_I32((int32_t)SELF_TEST_OK, (int32_t)self_test_run());

    fw_test_case_begin("self test code global mirrors the verdict");
    FW_ASSERT_EQ_U32((uint32_t)SELF_TEST_OK, g_self_test_code);
}
