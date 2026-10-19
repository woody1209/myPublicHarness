//#############################################################################
//
// FILE:   fw_test.h
//
// TITLE:  Minimal self-contained test framework for host-side unit tests.
//
// DESCRIPTION:
//   Why not Unity? The reference harness (C3M3-harness) pulls Unity via CMake
//   FetchContent, which needs network access and a working cmake. This machine
//   has neither, and an AI-facing harness must be able to run its regression
//   suite offline and deterministically. Hence this ~100 line framework: no
//   dependencies, no dynamic allocation, identical semantics on every host
//   compiler (gcc / clang / MSVC).
//
//   Usage:
//     fw_test_suite_begin("name");
//     FW_ASSERT_EQ_U32(expected, actual);   // increments the failure counter
//     return fw_test_summary();             // number of failures (0 = pass)
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#ifndef HARNESS_FW_TEST_H
#define HARNESS_FW_TEST_H

#include <stdint.h>
#include <stdbool.h>

//! Start a suite (prints a header).
void fw_test_suite_begin(const char *name);

//! Start a named case (prints the name; failures are attributed to it).
void fw_test_case_begin(const char *name);

//! Record a failure. Called through the FW_ASSERT_* macros.
void fw_test_fail(const char *file, int line, const char *expr);

//! Print the summary. \return number of failed assertions (0 = all green)
int fw_test_summary(void);

#define FW_ASSERT_TRUE(expr)      do { if (!(expr)) { fw_test_fail(__FILE__, __LINE__, #expr); } } while (0)
#define FW_ASSERT_FALSE(expr)     do { if  ((expr)) { fw_test_fail(__FILE__, __LINE__, #expr); } } while (0)
#define FW_ASSERT_EQ_I32(exp,act) do { int32_t e_ = (int32_t)(exp); int32_t a_ = (int32_t)(act); \
                                       if (e_ != a_) { fw_test_fail(__FILE__, __LINE__, "EQ_I32(" #exp ", " #act ")"); } } while (0)
#define FW_ASSERT_EQ_U32(exp,act) do { uint32_t e_ = (uint32_t)(exp); uint32_t a_ = (uint32_t)(act); \
                                       if (e_ != a_) { fw_test_fail(__FILE__, __LINE__, "EQ_U32(" #exp ", " #act ")"); } } while (0)
#define FW_ASSERT_EQ_STR(exp,act) do { const char *e_ = (const char *)(exp); const char *a_ = (const char *)(act); \
                                       if (!fw_test_str_eq(e_, a_)) { fw_test_fail(__FILE__, __LINE__, "EQ_STR(" #exp ", " #act ")"); } } while (0)

//! String equality helper (declared here so the macro above can use it).
bool fw_test_str_eq(const char *a, const char *b);

#endif // HARNESS_FW_TEST_H
