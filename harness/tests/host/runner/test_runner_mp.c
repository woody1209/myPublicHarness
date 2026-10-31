//#############################################################################
//
// FILE:   test_runner_mp.c
//
// TITLE:  Host test runner entry point (motor-pwm project).
//
// DESCRIPTION:
//   Registers every motor-pwm suite. modules.json (harness/tests/host) is the
//   declared truth: add the module there, and harness/tools/static-check.ps1
//   fails if this runner, CMakeLists.txt or the suite set disagrees.
//   The on-target self test (self_test.c) is executed here as well: it is pure
//   logic, so the very same code that runs at power-on on the board is proven
//   on the host (REQ-004 "no two truths").
//
//   v2 (spec 0002 T-005): this is the SECOND project's runner - one runner
//   executable per project, so identical module names (fmt, self_test) in
//   led-chaser and motor-pwm never collide.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "self_test.h"

void angle_gen_tests(void);
void svpwm_tests(void);
void mp_selftest_tests(void);

int main(void)
{
    int failures;

    fw_test_suite_begin("host unit tests (f280039c-harness, motor-pwm)");

    angle_gen_tests();
    svpwm_tests();
    mp_selftest_tests();

    failures = fw_test_summary();

    return (failures == 0) ? 0 : 1;
}
