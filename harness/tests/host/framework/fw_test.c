//#############################################################################
//
// FILE:   fw_test.c
//
// TITLE:  Minimal self-contained test framework (implementation).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include <stdio.h>

static int  g_failed = 0;
static int  g_cases = 0;
static const char *g_current_case = "(none)";

bool fw_test_str_eq(const char *a, const char *b)
{
    if ((a == 0) || (b == 0))
    {
        return false;
    }
    while ((*a != '\0') && (*b != '\0'))
    {
        if (*a != *b)
        {
            return false;
        }
        a++;
        b++;
    }
    return (*a == *b);
}

void fw_test_suite_begin(const char *name)
{
    printf("\n=== %s ===\n", (name != 0) ? name : "(unnamed)");
}

void fw_test_case_begin(const char *name)
{
    g_current_case = (name != 0) ? name : "(unnamed)";
    g_cases++;
    printf("  - %s\n", g_current_case);
}

void fw_test_fail(const char *file, int line, const char *expr)
{
    g_failed++;
    printf("      FAIL [%s:%d] %s\n", file, line, (expr != 0) ? expr : "");
}

int fw_test_summary(void)
{
    printf("\n----------------------------------------\n");
    printf("cases: %d, failures: %d -> %s\n", g_cases, g_failed,
           (g_failed == 0) ? "PASS" : "FAIL");

    if (g_failed == 0)
    {
        return 0;
    }
    return g_failed;
}
