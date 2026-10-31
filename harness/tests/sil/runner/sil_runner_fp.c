//#############################################################################
//
// FILE:   sil_runner_fp.c
//
// TITLE:  SIL runner entry point for the full-platform project.
//
// DESCRIPTION:
//   Software-In-the-Loop: the real firmware modules run on the host inside a
//   simulated main loop. Distinct from harness/tests/host (unit tests): SIL
//   asserts properties of TIME SERIES, not of single return values.
//
//   modules.json (harness/tests/sil) is the declared truth for this layer.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"

void sil_sched_timing_tests(void);
void sil_health_fsm_tests(void);

int main(void)
{
    int failures;

    fw_test_suite_begin("SIL (launchxl-f280039c-full-platform)");

    sil_sched_timing_tests();
    sil_health_fsm_tests();

    failures = fw_test_summary();

    return (failures == 0) ? 0 : 1;
}
