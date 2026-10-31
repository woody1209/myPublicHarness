//#############################################################################
//
// FILE:   test_runner_fp.c
//
// TITLE:  Host test runner entry point for the full-platform project.
//
// DESCRIPTION:
//   Per-project runner (modules.json v2): registers every full-platform
//   suite. modules.json (harness/tests/host) is the declared truth;
//   harness/tools/static-check.ps1 fails if this runner, CMakeLists.txt or
//   the suite set disagree.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "self_test.h"

void scheduler_tests(void);
void health_tests(void);
void fp_selftest_tests(void);
void adc_stat_tests(void);
void math_ref_tests(void);
void frame_gen_tests(void);

int main(void)
{
    int failures;

    fw_test_suite_begin("host unit tests (launchxl-f280039c-full-platform)");

    scheduler_tests();
    health_tests();
    fp_selftest_tests();
    adc_stat_tests();
    math_ref_tests();
    frame_gen_tests();

    failures = fw_test_summary();

    return (failures == 0) ? 0 : 1;
}
