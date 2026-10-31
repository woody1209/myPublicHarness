//#############################################################################
//
// FILE:   test_mp_selftest.c
//
// TITLE:  Host-side execution of the motor-pwm power-on self test.
//
//   self_test.c is pure logic: the very same object code that proves the
//   firmware at power-on on the board runs here on the host (REQ-004 "no two
//   truths"). The suite also asserts the debugger-visible global and adds
//   fmt reference vectors that are deliberately DIFFERENT from the ones
//   inside self_test.c, so a wrong reference cannot hide behind a shared one.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "self_test.h"
#include "fmt.h"

void mp_selftest_tests(void)
{
    char buf[24];

    fw_test_suite_begin("on-target self test executed on host");

    fw_test_case_begin("self_test_run() == SELF_TEST_OK");
    FW_ASSERT_EQ_I32((int32_t)SELF_TEST_OK, (int32_t)self_test_run());

    fw_test_case_begin("g_self_test_code is 0 for the debugger (HIL leg H3)");
    FW_ASSERT_EQ_U32((uint32_t)SELF_TEST_OK, g_self_test_code);

    // ---------------------------------------------------------- fmt vectors --
    fw_test_case_begin("fmt_u32_dec reference values (independent set)");
    FW_ASSERT_EQ_I32(1, fmt_u32_dec(1U, buf, sizeof(buf)));
    FW_ASSERT_EQ_STR("1", buf);
    FW_ASSERT_EQ_I32(10, fmt_u32_dec(1000000000U, buf, sizeof(buf)));
    FW_ASSERT_EQ_STR("1000000000", buf);

    fw_test_case_begin("fmt_u32_hex reference values (independent set)");
    FW_ASSERT_EQ_I32(3, fmt_u32_hex(0U, buf, sizeof(buf)));
    FW_ASSERT_EQ_STR("0x0", buf);
    FW_ASSERT_EQ_I32(4, fmt_u32_hex(0xABU, buf, sizeof(buf)));
    FW_ASSERT_EQ_STR("0xAB", buf);

    fw_test_case_begin("fmt rejects a buffer that cannot hold digits + NUL");
    buf[0] = 'X';
    FW_ASSERT_EQ_I32(2, fmt_u32_dec(42U, buf, 3U));       // "42" + NUL fits
    FW_ASSERT_EQ_STR("42", buf);
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(42U, buf, 2U));
    FW_ASSERT_EQ_I32('\0', buf[0]);                       // NUL on rejection
    FW_ASSERT_EQ_I32(4, fmt_u32_hex(0xABU, buf, 5U));     // "0xAB" + NUL fits
    FW_ASSERT_EQ_STR("0xAB", buf);
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_hex(0xABU, buf, 4U));
    FW_ASSERT_EQ_I32('\0', buf[0]);

    fw_test_case_begin("fmt rejects NULL destination and zero size");
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(5U, 0, 4U));
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_hex(5U, 0, 4U));
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(5U, buf, 0U));
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_hex(5U, buf, 0U));
}
