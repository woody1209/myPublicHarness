//#############################################################################
//
// FILE:   scheduler.h
//
// TITLE:  Cooperative task scheduler (pure logic, hardware independent).
//
// DESCRIPTION:
//   Period-based round-robin dispatcher for all platform subsystems. Pure
//   logic: depends only on <stdint.h>/<stdbool.h>/<stddef.h>, so the exact
//   same code runs in host unit tests and on the C28x target.
//
//   Contract:
//   - Tasks are registered with a period and an optional phase (offset) so
//     that same-period tasks do not all fire on the same millisecond.
//   - sched_run_due(now_ms) must be called once per main-loop iteration; it
//     invokes every task whose (now_ms - phase) has reached the next period
//     boundary. Tasks must not block or busy-wait.
//   - The scheduler tracks run counts and the worst observed lateness per
//     task so the health monitor (health.h) can judge "still working".
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_SCHEDULER_H
#define FULLPLATFORM_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//! Maximum number of registered tasks.
#define SCHED_MAX_TASKS               32U

//! \return the millisecond bucket a task with \p period_ms and \p phase_ms is
//!         due at for a given \p now_ms, or SCHED_NOT_DUE when it is not.
#define SCHED_NOT_DUE                 UINT32_MAX

typedef void (*sched_task_fn)(uint32_t now_ms);

typedef struct
{
    const char *name;            //!< short id (<=7 chars), for the health line
    sched_task_fn tick;          //!< called when due; must not block
    uint16_t period_ms;          //!< 0 = every iteration of the main loop
    uint16_t phase_ms;           //!< de-synchronisation offset, < period_ms
    /* runtime state, maintained by the scheduler: */
    uint32_t next_due_ms;        //!< absolute due time of the next run
    uint32_t run_count;          //!< completed ticks
    int32_t  max_late_ms;        //!< worst observed (run - due) in ms
} sched_task_t;

//! Reset the scheduler table (clears all slots and runtime state).
void sched_init(uint32_t now_ms);

//! Register a task. \return slot index (>=0) or -1 when the table is full or
//! the descriptor is invalid (NULL name/tick, or phase >= period).
int16_t sched_register(sched_task_t *task);

//! Run every task that is due at \p now_ms. Tasks run in registration order;
//! a task that overruns its period runs once and its next_due resynchronises
//! to the first period boundary after now (no catch-up backlog).
void sched_run_due(uint32_t now_ms);

//! \return number of registered tasks.
uint16_t sched_task_count(void);

//! \return true when the task has missed its window: the previous run was
//!         more than tol_ms late, or it has not run within period + tol_ms.
bool sched_task_late(const sched_task_t *task, uint32_t now_ms, uint16_t tol_ms);

//! \return the due bucket for (period, phase, now), or SCHED_NOT_DUE.
//!         Exposed for unit tests; sched_run_due uses it internally.
uint32_t sched_due_bucket(uint16_t period_ms, uint16_t phase_ms, uint32_t now_ms);

#endif // FULLPLATFORM_SCHEDULER_H
