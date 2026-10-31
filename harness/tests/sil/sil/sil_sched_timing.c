//#############################################################################
//
// FILE:   sil_sched_timing.c
//
// TITLE:  SIL (Software-In-the-Loop) - cooperative scheduler TIMING.
//
// DESCRIPTION:
//   This is NOT a unit test. Unit tests (harness/tests/host) assert one
//   function's return value for one input. SIL runs the real module INSIDE a
//   simulated main loop for thousands of milliseconds and asserts properties
//   of the resulting TIME SERIES:
//
//     - every task fires exactly floor(window / period) times,
//     - no task is ever late when the loop keeps up (max_late_ms == 0),
//     - phases actually de-synchronise same-period tasks (bounded concurrency),
//     - a long loop stall produces NO catch-up backlog (one run, not N),
//     - lateness is RECORDED when the loop stalls (honest evidence).
//
//   That last pair is the whole point of this layer: a scheduler that silently
//   swallows a stall and a scheduler that replays 90 missed ticks both look
//   "fine" to a unit test, and both are wrong on a real motor drive.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "scheduler.h"

#include <string.h>

/* ------------------------------------------------------------------ fixtures */

/* Simulated main-loop window and the per-millisecond concurrency histogram. */
#define SIM_WINDOW_MS           1000U
#define SIM_CONCURRENCY_BINS    1200U

static uint16_t g_concurrency[SIM_CONCURRENCY_BINS];
static uint32_t g_last_fire_ms[8];

/* One shared callback body: record the firing time for slot <n>. */
#define SIL_TASK_BODY(n)                                       \
    static void sil_task_##n(uint32_t now_ms)                  \
    {                                                          \
        g_last_fire_ms[n] = now_ms;                            \
        if (now_ms < SIM_CONCURRENCY_BINS) { g_concurrency[now_ms]++; } \
    }

SIL_TASK_BODY(0)
SIL_TASK_BODY(1)
SIL_TASK_BODY(2)
SIL_TASK_BODY(3)
SIL_TASK_BODY(4)

static void sil_reset_observers(void)
{
    memset(g_concurrency, 0, sizeof(g_concurrency));
    memset(g_last_fire_ms, 0, sizeof(g_last_fire_ms));
}

/* ---- case 1: nominal cadence over a 1000 ms window ------------------------- */
static void sil_case_nominal_cadence(void)
{
    uint32_t now;
    sched_task_t t_fast, t_slow_a, t_slow_b, t_centi, t_second;

    fw_test_case_begin("nominal cadence: 1000 ms window, 1/10/10/100/1000 ms tasks");

    sil_reset_observers();
    sched_init(0U);

    /* Deliberately two same-period tasks with different phases: this is the
       de-synchronisation contract in scheduler.h. */
    memset(&t_fast,   0, sizeof(t_fast));
    memset(&t_slow_a, 0, sizeof(t_slow_a));
    memset(&t_slow_b, 0, sizeof(t_slow_b));
    memset(&t_centi,  0, sizeof(t_centi));
    memset(&t_second, 0, sizeof(t_second));

    t_fast.name = "fast";   t_fast.tick = sil_task_0;   t_fast.period_ms = 1U;    t_fast.phase_ms = 0U;
    t_slow_a.name = "pwm";  t_slow_a.tick = sil_task_1; t_slow_a.period_ms = 10U; t_slow_a.phase_ms = 0U;
    t_slow_b.name = "adc";  t_slow_b.tick = sil_task_2; t_slow_b.period_ms = 10U; t_slow_b.phase_ms = 5U;
    t_centi.name = "stat";  t_centi.tick = sil_task_3;  t_centi.period_ms = 100U; t_centi.phase_ms = 0U;
    t_second.name = "hlth"; t_second.tick = sil_task_4; t_second.period_ms = 1000U; t_second.phase_ms = 0U;

    FW_ASSERT_TRUE(sched_register(&t_fast)   >= 0);
    FW_ASSERT_TRUE(sched_register(&t_slow_a) >= 0);
    FW_ASSERT_TRUE(sched_register(&t_slow_b) >= 0);
    FW_ASSERT_TRUE(sched_register(&t_centi)  >= 0);
    FW_ASSERT_TRUE(sched_register(&t_second) >= 0);

    /* Simulated main loop: one sched_run_due() per millisecond. */
    for (now = 0U; now <= SIM_WINDOW_MS; now++) {
        sched_run_due(now);
    }

    /* window 0..1000 inclusive = 1001 loop iterations. */
    FW_ASSERT_EQ_U32(1001U, t_fast.run_count);
    /* period 10, phase 0 -> fires at 0,10,...,1000 = 101 runs. */
    FW_ASSERT_EQ_U32(101U, t_slow_a.run_count);
    /* period 10, phase 5 -> fires at 5,15,...,995 = 100 runs. */
    FW_ASSERT_EQ_U32(100U, t_slow_b.run_count);
    /* period 100, phase 0 -> 0,100,...,1000 = 11 runs. */
    FW_ASSERT_EQ_U32(11U, t_centi.run_count);
    /* period 1000, phase 0 -> 0 and 1000 = 2 runs. */
    FW_ASSERT_EQ_U32(2U, t_second.run_count);

    /* A loop that keeps up must never register lateness. */
    FW_ASSERT_EQ_I32(0, t_fast.max_late_ms);
    FW_ASSERT_EQ_I32(0, t_slow_a.max_late_ms);
    FW_ASSERT_EQ_I32(0, t_slow_b.max_late_ms);
    FW_ASSERT_EQ_I32(0, t_centi.max_late_ms);
    FW_ASSERT_EQ_I32(0, t_second.max_late_ms);

    FW_ASSERT_FALSE(sched_task_late(&t_slow_a, SIM_WINDOW_MS, 0U));
}

/* ---- case 2: phases de-synchronise same-period tasks ----------------------- */
static void sil_case_phase_desync(void)
{
    uint32_t now;
    uint32_t worst = 0U;
    uint32_t both_on_same_ms = 0U;
    sched_task_t t_a, t_b;

    fw_test_case_begin("phase offset keeps two 10 ms tasks off the same millisecond");

    sil_reset_observers();
    sched_init(0U);

    memset(&t_a, 0, sizeof(t_a));
    memset(&t_b, 0, sizeof(t_b));
    t_a.name = "pwm"; t_a.tick = sil_task_0; t_a.period_ms = 10U; t_a.phase_ms = 0U;
    t_b.name = "adc"; t_b.tick = sil_task_1; t_b.period_ms = 10U; t_b.phase_ms = 5U;

    sched_register(&t_a);
    sched_register(&t_b);

    for (now = 0U; now <= SIM_WINDOW_MS; now++) {
        sched_run_due(now);
    }

    /* Concurrency histogram: the two 10 ms tasks must NEVER land on the same
       millisecond once their phases are 5 ms apart. */
    for (now = 0U; now < SIM_WINDOW_MS; now++) {
        if (g_concurrency[now] > worst) { worst = g_concurrency[now]; }
        if (g_concurrency[now] >= 2U) { both_on_same_ms++; }
    }

    FW_ASSERT_EQ_U32(0U, both_on_same_ms);
    FW_ASSERT_TRUE(worst <= 1U);
}

/* ---- case 3: a long stall must NOT produce a catch-up backlog -------------- */
static void sil_case_no_backlog_after_stall(void)
{
    uint32_t now;
    uint32_t runs_before;
    sched_task_t t_10ms;

    fw_test_case_begin("900 ms stall: one run, lateness recorded, no catch-up");

    sil_reset_observers();
    sched_init(0U);

    memset(&t_10ms, 0, sizeof(t_10ms));
    t_10ms.name = "pwm"; t_10ms.tick = sil_task_0;
    t_10ms.period_ms = 10U; t_10ms.phase_ms = 0U;
    sched_register(&t_10ms);

    /* Warm up: 0..99 ms. */
    for (now = 0U; now < 100U; now++) {
        sched_run_due(now);
    }
    runs_before = t_10ms.run_count;

    /* The loop stalls for 900 ms (a blocking flash write, a SCI burst, ...).
       The next sched_run_due() sees now = 1000. */
    sched_run_due(1000U);

    /* Contract: exactly ONE run, not the 90 that a naive "while (due)" loop
       would replay. Replaying them would flood the motor control loop with
       stale set-points long after the stall ended. */
    FW_ASSERT_EQ_U32(runs_before + 1U, t_10ms.run_count);

    /* But the stall must still be VISIBLE: max_late_ms records it. A scheduler
       that hides the stall would make health monitoring blind. */
    FW_ASSERT_EQ_I32(900, t_10ms.max_late_ms);
    FW_ASSERT_TRUE(sched_task_late(&t_10ms, 1000U, 0U));

    /* And it resynchronises: the next due boundary is 1010, not 10. */
    sched_run_due(1009U);
    FW_ASSERT_EQ_U32(runs_before + 1U, t_10ms.run_count);
    sched_run_due(1010U);
    FW_ASSERT_EQ_U32(runs_before + 2U, t_10ms.run_count);
}

/* ---- case 4: task registry guards ------------------------------------------ */
static void sil_case_registry_guards(void)
{
    sched_task_t bad_phase, null_tick;

    fw_test_case_begin("registry rejects phase >= period and NULL tick");

    sil_reset_observers();
    sched_init(0U);

    memset(&bad_phase, 0, sizeof(bad_phase));
    bad_phase.name = "bad"; bad_phase.tick = sil_task_0;
    bad_phase.period_ms = 10U; bad_phase.phase_ms = 10U;   /* == period -> reject */
    FW_ASSERT_EQ_I32(-1, sched_register(&bad_phase));

    memset(&null_tick, 0, sizeof(null_tick));
    null_tick.name = "null"; null_tick.tick = NULL;
    null_tick.period_ms = 10U; null_tick.phase_ms = 0U;
    FW_ASSERT_EQ_I32(-1, sched_register(&null_tick));

    FW_ASSERT_EQ_U32(0U, sched_task_count());
}

/* ---- case 5: period-0 task runs every loop iteration ------------------------ */
static void sil_case_period_zero(void)
{
    uint32_t now;
    sched_task_t t_always;

    fw_test_case_begin("period 0 = every main-loop iteration");

    sil_reset_observers();
    sched_init(0U);

    memset(&t_always, 0, sizeof(t_always));
    t_always.name = "bg"; t_always.tick = sil_task_0;
    t_always.period_ms = 0U; t_always.phase_ms = 0U;
    sched_register(&t_always);

    for (now = 0U; now < 500U; now++) {
        sched_run_due(now);
    }
    FW_ASSERT_EQ_U32(500U, t_always.run_count);
    /* period 0 is never "late" - there is no window to miss. */
    FW_ASSERT_FALSE(sched_task_late(&t_always, 500U, 0U));
}

void sil_sched_timing_tests(void)
{
    fw_test_suite_begin("SIL scheduler timing (launchxl-f280039c-full-platform)");
    sil_case_nominal_cadence();
    sil_case_phase_desync();
    sil_case_no_backlog_after_stall();
    sil_case_registry_guards();
    sil_case_period_zero();
}
