//#############################################################################
//
// FILE:   test_fmt.c
//
// TITLE:  Host-side unit tests for the bounded formatting module (REQ-005).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "fmt.h"

void fmt_tests(void)
{
    char buf[24];
    int32_t n;

    fw_test_suite_begin("fmt");

    fw_test_case_begin("decimal: zero and small values");
    n = fmt_u32_dec(0U, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(1, n);
    FW_ASSERT_EQ_STR("0", buf);

    n = fmt_u32_dec(7U, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(1, n);
    FW_ASSERT_EQ_STR("7", buf);

    n = fmt_u32_dec(12345U, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(5, n);
    FW_ASSERT_EQ_STR("12345", buf);

    fw_test_case_begin("decimal: UINT32_MAX boundary");
    n = fmt_u32_dec(0xFFFFFFFFU, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(10, n);
    FW_ASSERT_EQ_STR("4294967295", buf);

    fw_test_case_begin("hexadecimal: prefix, upper case, no leading zeros");
    n = fmt_u32_hex(0U, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(3, n);
    FW_ASSERT_EQ_STR("0x0", buf);

    n = fmt_u32_hex(0x1A2BU, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(6, n);
    FW_ASSERT_EQ_STR("0x1A2B", buf);

    n = fmt_u32_hex(0xFFFFFFFFU, buf, sizeof(buf));
    FW_ASSERT_EQ_I32(10, n);
    FW_ASSERT_EQ_STR("0xFFFFFFFF", buf);

    fw_test_case_begin("bounded: buffer too small is reported, not overrun");
    buf[0] = 'X';
    buf[1] = 'Y';
    buf[2] = 'Z';
    buf[3] = 'W';
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(1000U, buf, 4U));  // needs 5 bytes

    buf[0] = 'X';
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(0U, buf, 1U));     // needs 2 bytes

    buf[0] = 'X';
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_hex(0x1A2BU, buf, 6U)); // needs 7 bytes

    fw_test_case_begin("bounded: zero size and NULL destination are rejected");
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(1U, buf, 0U));
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_hex(1U, buf, 0U));
    FW_ASSERT_EQ_I32(FMT_ERR_TOO_SMALL, fmt_u32_dec(1U, 0, sizeof(buf)));

    fw_test_case_begin("bounded: exact fit succeeds (no NUL overruns)");
    n = fmt_u32_dec(1000U, buf, 5U);
    FW_ASSERT_EQ_I32(4, n);
    FW_ASSERT_EQ_STR("1000", buf);
}
