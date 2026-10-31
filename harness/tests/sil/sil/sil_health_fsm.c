//#############################################################################
//
// FILE:   sil_health_fsm.c
//
// TITLE:  SIL (Software-In-the-Loop) - health monitor failure/recovery FSM.
//
// DESCRIPTION:
//   Unit tests can tell you health_fail() increments a counter. Only a
//   time-series run can tell you whether the PLATFORM actually behaves the way
//   the safety argument claims:
//
//     - a MUST subsystem fails 1x, 2x -> platform keeps running,
//     - 3x consecutive -> fail-safe latches (HEALTH_ERR_THRESHOLD_MUST),
//     - one good beat -> latched state CLEARS (recoverable, not a brick),
//     - a SHOULD/COULD subsystem can fail forever without ever latching,
//     - the fail-safe decision is driven by CONSECUTIVE errors, so a single
//       good beat in between resets the budget (no drift-to-latch).
//
//   This is the software half of REQ-020/021. The on-target half is the HIL
//   leg, which reads the same state out of the running target.
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "health.h"

/* ---- case 1: MUST subsystem latches only at the threshold ------------------- */
static void sil_case_must_threshold(void)
{
    int16_t id;

    fw_test_case_begin("MUST subsystem: fail-safe latches at 3 consecutive errors");

    health_init();
    id = health_register("pwm", HEALTH_SEVERITY_MUST);
    FW_ASSERT_TRUE(id >= 0);
    FW_ASSERT_FALSE(health_should_failsafe());

    health_fail(id);
    FW_ASSERT_FALSE(health_should_failsafe());   /* 1 < 3 */

    health_fail(id);
    FW_ASSERT_FALSE(health_should_failsafe());   /* 2 < 3 */

    health_fail(id);
    FW_ASSERT_TRUE(health_should_failsafe());    /* 3 >= 3 -> latch */
    FW_ASSERT_EQ_U32(3U, health_err_total());
    FW_ASSERT_EQ_I32(id, health_worst());
}

/* ---- case 2: a single good beat clears the consecutive budget --------------- */
static void sil_case_recovery_clears(void)
{
    int16_t id;

    fw_test_case_begin("one good beat clears the consecutive-error budget");

    health_init();
    id = health_register("pwm", HEALTH_SEVERITY_MUST);

    health_fail(id);
    health_fail(id);
    health_beat(id);                             /* recovered */
    FW_ASSERT_FALSE(health_should_failsafe());

    health_fail(id);
    health_fail(id);
    FW_ASSERT_FALSE(health_should_failsafe());   /* only 2 consecutive again */

    health_fail(id);
    FW_ASSERT_TRUE(health_should_failsafe());

    /* Recovery is real: beats accumulate and the latch releases. */
    health_beat(id);
    FW_ASSERT_FALSE(health_should_failsafe());
    /* Two beats were recorded across the case (one mid-case, one here). */
    FW_ASSERT_EQ_U32(2U, health_min_beats());
}

/* ---- case 3: SHOULD / COULD never take the platform down -------------------- */
static void sil_case_degrade_only(void)
{
    int16_t id_should, id_could;
    uint32_t i;

    fw_test_case_begin("SHOULD/COULD subsystems degrade but never latch fail-safe");

    health_init();
    id_should = health_register("fsi", HEALTH_SEVERITY_SHOULD);
    id_could  = health_register("acc", HEALTH_SEVERITY_COULD);
    FW_ASSERT_TRUE(id_should >= 0);
    FW_ASSERT_TRUE(id_could  >= 0);

    /* 200 consecutive failures each - far past the MUST threshold of 3. */
    for (i = 0U; i < 200U; i++) {
        health_fail(id_should);
        health_fail(id_could);
    }

    FW_ASSERT_FALSE(health_should_failsafe());
    FW_ASSERT_EQ_U32(400U, health_err_total());

    /* They ARE visible in the map / worst report - degrade is observable. */
    FW_ASSERT_TRUE((health_map() & (1UL << id_should)) != 0U);
    FW_ASSERT_TRUE((health_map() & (1UL << id_could)) != 0U);
    FW_ASSERT_TRUE(health_worst() == id_should || health_worst() == id_could);
}

/* ---- case 4: mixed fleet - only the MUST failure decides -------------------- */
static void sil_case_mixed_fleet(void)
{
    int16_t id_pwm, id_can, id_cla;

    fw_test_case_begin("mixed fleet: noisy COULD subsystem must not arm fail-safe");

    health_init();
    id_pwm = health_register("pwm", HEALTH_SEVERITY_MUST);
    id_can = health_register("can", HEALTH_SEVERITY_SHOULD);
    id_cla = health_register("cla", HEALTH_SEVERITY_COULD);

    /* The healthy MUST keeps beating while a noisy COULD fails every tick. */
    for (uint32_t i = 0U; i < 50U; i++) {
        health_beat(id_pwm);
        health_fail(id_can);
        health_fail(id_cla);
    }
    FW_ASSERT_FALSE(health_should_failsafe());
    FW_ASSERT_EQ_U32(50U, health_min_beats());

    /* Now the MUST itself breaks: 3 ticks and the platform must go safe. */
    health_fail(id_pwm);
    health_fail(id_pwm);
    FW_ASSERT_FALSE(health_should_failsafe());
    health_fail(id_pwm);
    FW_ASSERT_TRUE(health_should_failsafe());
}

/* ---- case 5: registry limits and name lookup ------------------------------- */
static void sil_case_registry(void)
{
    int16_t id0, id1;

    fw_test_case_begin("registry: name lookup, count, and NULL rejection");

    health_init();
    FW_ASSERT_EQ_I32(-1, health_register(NULL, HEALTH_SEVERITY_MUST));
    FW_ASSERT_EQ_U32(0U, health_count());

    id0 = health_register("a", HEALTH_SEVERITY_MUST);
    id1 = health_register("b", HEALTH_SEVERITY_SHOULD);
    FW_ASSERT_EQ_U32(2U, health_count());
    FW_ASSERT_TRUE(fw_test_str_eq("a", health_name(id0)));
    FW_ASSERT_TRUE(fw_test_str_eq("b", health_name(id1)));
    FW_ASSERT_TRUE(health_name(99) == NULL);
    FW_ASSERT_TRUE(health_name(-1) == NULL);

    /* Out-of-range ids are ignored, not asserted on: the monitor runs in the
       platform's main loop and must never become the crash source. */
    health_fail(99);
    health_beat(-1);
    FW_ASSERT_EQ_U32(0U, health_err_total());
}

void sil_health_fsm_tests(void)
{
    fw_test_suite_begin("SIL health FSM (launchxl-f280039c-full-platform)");
    sil_case_must_threshold();
    sil_case_recovery_clears();
    sil_case_degrade_only();
    sil_case_mixed_fleet();
    sil_case_registry();
}
