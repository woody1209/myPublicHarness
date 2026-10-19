//#############################################################################
//
// FILE:   test_runner.c
//
// TITLE:  Host test runner entry point.
//
// DESCRIPTION:
//   Registers every module suite. modules.json (harness/tests/host) is the
//   declared truth: add the module there, and harness/tools/static-check.ps1
//   fails if this runner, CMakeLists.txt or the suite set disagrees.
//   The on-target self test (self_test.c) is executed here as well: it is pure
//   logic, so the very same code that runs at power-on on the board is proven
//   on the host (REQ-004 "no two truths").
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "self_test.h"

void led_pattern_tests(void);
void fmt_tests(void);

int main(void)
{
    int failures;

    fw_test_suite_begin("host unit tests (f280039c-harness)");

    led_pattern_tests();
    fmt_tests();

    fw_test_suite_begin("on-target self test executed on host");
    fw_test_case_begin("self_test_run() == SELF_TEST_OK");
    FW_ASSERT_EQ_I32((int32_t)SELF_TEST_OK, (int32_t)self_test_run());

    failures = fw_test_summary();

    return (failures == 0) ? 0 : 1;
}
