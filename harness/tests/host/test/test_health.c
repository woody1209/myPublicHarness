//#############################################################################
//
// FILE:   test_health.c
//
// TITLE:  Host unit tests for the health monitor pure-logic module (spec 0002).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "health.h"

void health_tests(void)
{
    int16_t must_id;
    int16_t should_id;

    fw_test_suite_begin("health");

    fw_test_case_begin("register: defaults per severity");
    health_init();
    must_id = health_register("must", HEALTH_SEVERITY_MUST);
    should_id = health_register("shld", HEALTH_SEVERITY_SHOULD);
    FW_ASSERT_TRUE(must_id >= 0);
    FW_ASSERT_TRUE(should_id >= 0);
    FW_ASSERT_TRUE(must_id != should_id);
    FW_ASSERT_EQ_U32(2U, (uint32_t)health_count());
    FW_ASSERT_TRUE(health_name(must_id) != NULL);
    FW_ASSERT_TRUE(health_name(-1) == NULL);
    FW_ASSERT_TRUE(health_name(999) == NULL);
    FW_ASSERT_EQ_U32(0U, health_err_total());
    FW_ASSERT_EQ_U32(0U, health_map());
    FW_ASSERT_FALSE(health_should_failsafe());

    fw_test_case_begin("map and totals count every failure");
    health_fail(must_id);
    health_fail(should_id);
    health_fail(should_id);
    FW_ASSERT_EQ_U32(3U, health_err_total());
    FW_ASSERT_EQ_U32((1UL << (uint32_t)must_id) | (1UL << (uint32_t)should_id),
                     health_map());

    fw_test_case_begin("MUST latches at consecutive threshold");
    health_init();
    must_id = health_register("must", HEALTH_SEVERITY_MUST);
    health_fail(must_id);
    health_fail(must_id);
    FW_ASSERT_FALSE(health_should_failsafe());   /* 2 < HEALTH_ERR_THRESHOLD_MUST */
    health_fail(must_id);
    FW_ASSERT_TRUE(health_should_failsafe());    /* 3 == threshold */

    fw_test_case_begin("beat clears the consecutive counter (recovery)");
    health_beat(must_id);
    FW_ASSERT_FALSE(health_should_failsafe());
    /* totals keep history: 3 fails + nothing erased */
    FW_ASSERT_EQ_U32(3U, health_err_total());

    fw_test_case_begin("SHOULD never latches fail-safe");
    health_init();
    should_id = health_register("shld", HEALTH_SEVERITY_SHOULD);
    {
        uint16_t i;

        for (i = 0U; i < 100U; i++) { health_fail(should_id); }
    }
    FW_ASSERT_FALSE(health_should_failsafe());
    FW_ASSERT_EQ_U32(100U, health_err_total());

    fw_test_case_begin("worst picks the biggest consecutive counter");
    health_init();
    must_id = health_register("a", HEALTH_SEVERITY_MUST);
    should_id = health_register("b", HEALTH_SEVERITY_SHOULD);
    health_fail(must_id);
    health_fail(should_id);
    health_fail(should_id);
    FW_ASSERT_EQ_I32(should_id, health_worst());
    health_fail(must_id);
    health_fail(must_id);
    FW_ASSERT_EQ_I32(must_id, health_worst());

    fw_test_case_begin("min beats over MUST monitors only");
    health_init();
    must_id = health_register("m1", HEALTH_SEVERITY_MUST);
    should_id = health_register("s1", HEALTH_SEVERITY_SHOULD);
    health_beat(must_id);
    health_beat(must_id);
    FW_ASSERT_EQ_U32(2U, health_min_beats());
    {
        uint16_t i;

        for (i = 0U; i < 5U; i++) { health_beat(should_id); }
    }
    FW_ASSERT_EQ_U32(2U, health_min_beats());    /* SHOULD does not lower it */
    health_fail(must_id);                        /* a failed beat still counts */
    health_beat(must_id);
    FW_ASSERT_EQ_U32(3U, health_min_beats());

    fw_test_case_begin("invalid ids are ignored, not fatal");
    health_init();
    health_beat(-1);
    health_beat(42);
    health_fail(-1);
    health_fail(42);
    FW_ASSERT_EQ_U32(0U, health_err_total());
    FW_ASSERT_EQ_U32(0U, (uint32_t)health_count());

    fw_test_case_begin("table capacity is rejected cleanly");
    health_init();
    {
        uint16_t i;
        int16_t rc = 0;
        int16_t last = 0;

        for (i = 0U; i < (HEALTH_MAX_MONITORS + 1U); i++)
        {
            rc = health_register("x", HEALTH_SEVERITY_COULD);
            if (i < HEALTH_MAX_MONITORS) { FW_ASSERT_TRUE(rc >= 0); }
            last = rc;
        }
        FW_ASSERT_TRUE(last < 0);
    }

    health_init();
}
