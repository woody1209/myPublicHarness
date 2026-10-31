//#############################################################################
//
// FILE:   test_scheduler.c
//
// TITLE:  Host unit tests for the scheduler pure-logic module (spec 0002).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "scheduler.h"

static uint32_t t_ticks_a;
static uint32_t t_ticks_b;

static void tick_a(uint32_t now_ms) { (void)now_ms; t_ticks_a++; }
static void tick_b(uint32_t now_ms) { (void)now_ms; t_ticks_b++; }

static void make_task(sched_task_t *t, const char *name, sched_task_fn fn,
                      uint16_t period, uint16_t phase)
{
    t->name = name;
    t->tick = fn;
    t->period_ms = period;
    t->phase_ms = phase;
    t->next_due_ms = 0U;
    t->run_count = 0U;
    t->max_late_ms = 0;
}

void scheduler_tests(void)
{
    sched_task_t a;
    sched_task_t b;

    fw_test_suite_begin("scheduler");

    /* due-bucket reference vectors */
    fw_test_case_begin("due bucket: exact boundaries");
    FW_ASSERT_EQ_U32(0U, sched_due_bucket(200U, 0U, 0U));
    FW_ASSERT_EQ_U32(0U, sched_due_bucket(200U, 0U, 199U));
    FW_ASSERT_EQ_U32(200U, sched_due_bucket(200U, 0U, 200U));
    FW_ASSERT_EQ_U32(400U, sched_due_bucket(200U, 0U, 401U));

    fw_test_case_begin("due bucket: phase offset");
    FW_ASSERT_EQ_U32(50U, sched_due_bucket(200U, 50U, 205U));
    FW_ASSERT_EQ_U32(250U, sched_due_bucket(200U, 50U, 260U));

    fw_test_case_begin("due bucket: invalid and every-pass descriptors");
    FW_ASSERT_EQ_U32(SCHED_NOT_DUE, sched_due_bucket(200U, 200U, 0U));
    FW_ASSERT_EQ_U32(SCHED_NOT_DUE, sched_due_bucket(200U, 201U, 999U));
    FW_ASSERT_EQ_U32(1234U, sched_due_bucket(0U, 0U, 1234U));

    /* registration validation */
    fw_test_case_begin("register: rejects invalid descriptors");
    sched_init(0U);
    {
        sched_task_t bad;

        bad.name = "bad";
        bad.tick = &tick_a;
        bad.period_ms = 10U;
        bad.phase_ms = 10U;     /* phase == period -> invalid */
        bad.next_due_ms = 0U;
        bad.run_count = 0U;
        bad.max_late_ms = 0;
        FW_ASSERT_TRUE(sched_register(&bad) < 0);
        bad.phase_ms = 11U;
        FW_ASSERT_TRUE(sched_register(&bad) < 0);
        bad.name = NULL;
        bad.phase_ms = 0U;
        FW_ASSERT_TRUE(sched_register(&bad) < 0);
        bad.tick = NULL;
        bad.name = "bad2";
        FW_ASSERT_TRUE(sched_register(&bad) < 0);
        FW_ASSERT_EQ_U32(0U, (uint32_t)sched_task_count());
    }

    /* periodic dispatch, exactly once per period boundary */
    fw_test_case_begin("run due: one run per boundary, no double fire");
    t_ticks_a = 0U;
    make_task(&a, "a", &tick_a, 10U, 0U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&a) >= 0);
    {
        uint32_t now;

        for (now = 0U; now <= 100U; now++)
        {
            sched_run_due(now);
        }
        FW_ASSERT_EQ_U32(11U, t_ticks_a);      /* 0,10,...,100 */
        FW_ASSERT_EQ_U32(11U, a.run_count);
    }

    /* no catch-up backlog after a stall: a 70 ms stall adds no extra runs */
    fw_test_case_begin("run due: resync after stall (no backlog)");
    t_ticks_b = 0U;
    make_task(&b, "b", &tick_b, 10U, 0U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&b) >= 0);
    {
        uint32_t now;

        for (now = 0U; now <= 20U; now++)      /* runs at 0,10,20 */
        {
            sched_run_due(now);
        }
        for (now = 21U; now <= 90U; now++)     /* stall: nothing scheduled */
        {
            /* (no calls - simulates a blocked main loop) */
        }
        for (now = 91U; now <= 100U; now++)    /* resumes before t=100 */
        {
            sched_run_due(now);
        }
        /* 0,10,20 then one late run at resume (t=91) and the next boundary
           (t=100): 5 runs instead of 11 - the 7 missed periods do NOT
           backlog, and the resynchronised task is back on its grid. */
        FW_ASSERT_EQ_U32(5U, t_ticks_b);
        FW_ASSERT_EQ_I32(61, b.max_late_ms);   /* resumed 61 ms past due */
    }

    /* phase de-synchronises two same-period tasks */
    fw_test_case_begin("run due: phase offsets interleave tasks");
    t_ticks_a = 0U;
    t_ticks_b = 0U;
    make_task(&a, "a", &tick_a, 10U, 0U);
    make_task(&b, "b", &tick_b, 10U, 5U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&a) >= 0);
    FW_ASSERT_TRUE(sched_register(&b) >= 0);
    sched_run_due(0U);
    sched_run_due(4U);
    FW_ASSERT_EQ_U32(1U, t_ticks_a);
    FW_ASSERT_EQ_U32(0U, t_ticks_b);
    sched_run_due(5U);
    FW_ASSERT_EQ_U32(1U, t_ticks_b);

    /* every-pass task (period 0) */
    fw_test_case_begin("run due: period 0 runs every iteration");
    t_ticks_a = 0U;
    make_task(&a, "fast", &tick_a, 0U, 0U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&a) >= 0);
    sched_run_due(0U);
    sched_run_due(0U);
    sched_run_due(1U);
    FW_ASSERT_EQ_U32(3U, t_ticks_a);

    /* lateness tracking: the first run is due at t=0 (phase 0), so running
       the loop first at t=25 makes that run 25 ms late. */
    fw_test_case_begin("late tracking and threshold helper");
    t_ticks_a = 0U;
    make_task(&a, "late", &tick_a, 10U, 0U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&a) >= 0);
    sched_run_due(25U);                        /* 25 ms late on first run */
    FW_ASSERT_EQ_I32(25, a.max_late_ms);
    FW_ASSERT_TRUE(sched_task_late(&a, 25U, 10U));
    FW_ASSERT_TRUE(sched_task_late(&a, 25U, 20U));
    FW_ASSERT_FALSE(sched_task_late(&a, 25U, 30U));

    /* never-run task goes late once period + tolerance has passed */
    fw_test_case_begin("late tracking: never-run task");
    t_ticks_b = 0U;
    make_task(&b, "dead", &tick_b, 10U, 0U);
    sched_init(0U);
    FW_ASSERT_TRUE(sched_register(&b) >= 0);
    FW_ASSERT_FALSE(sched_task_late(&b, 12U, 10U));
    FW_ASSERT_TRUE(sched_task_late(&b, 21U, 10U));

    /* table capacity */
    fw_test_case_begin("register: table full is rejected");
    sched_init(0U);
    {
        static sched_task_t many[SCHED_MAX_TASKS + 1U];
        uint16_t i;
        int16_t rc = 0;
        int16_t last = 0;

        for (i = 0U; i < (SCHED_MAX_TASKS + 1U); i++)
        {
            make_task(&many[i], "m", &tick_a, 10U, (uint16_t)(i % 10U));
            rc = sched_register(&many[i]);
            if (i < SCHED_MAX_TASKS) { FW_ASSERT_TRUE(rc >= 0); }
            last = rc;
        }
        FW_ASSERT_TRUE(last < 0);
    }

    sched_init(0U);
}
