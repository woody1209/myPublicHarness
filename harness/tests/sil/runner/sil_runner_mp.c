//#############################################################################
//
// FILE:   sil_runner_mp.c
//
// TITLE:  SIL runner entry point for the motor-pwm project.
//
// DESCRIPTION:
//   Runs the real angle generator + SVPWM modulator through whole electrical
//   revolutions on the host and asserts waveform-level properties. This is the
//   layer between the unit tests (harness/tests/host) and the board
//   (harness/tools/dss/hil_test_mp.js).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"

void sil_motor_wave_tests(void);

int main(void)
{
    int failures;

    fw_test_suite_begin("SIL (launchxl-f280039c-motor-pwm)");

    sil_motor_wave_tests();

    failures = fw_test_summary();

    return (failures == 0) ? 0 : 1;
}
