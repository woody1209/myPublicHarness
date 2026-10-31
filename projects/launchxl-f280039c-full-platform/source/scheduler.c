//#############################################################################
//
// FILE:   scheduler.c
//
// TITLE:  Cooperative task scheduler (pure logic).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "scheduler.h"

#define SCHED_SLOT_EMPTY   ((sched_task_t *)0)

/* NOT static: diag21 dumps it for board bring-up */
sched_task_t *sched_table[SCHED_MAX_TASKS];
static uint16_t sched_used;

void sched_init(uint32_t now_ms)
{
    uint16_t i;

    (void)now_ms;
    for (i = 0U; i < SCHED_MAX_TASKS; i++)
    {
        sched_table[i] = SCHED_SLOT_EMPTY;
    }
    sched_used = 0U;
}

uint32_t sched_due_bucket(uint16_t period_ms, uint16_t phase_ms, uint32_t now_ms)
{
    uint32_t bucket;

    if (period_ms == 0U)
    {
        return now_ms;                     /* every iteration */
    }
    if (phase_ms >= period_ms)
    {
        return SCHED_NOT_DUE;              /* invalid descriptor */
    }

    /* Bucket = largest k*period + phase that is <= now, then the NEXT such
       boundary after that (a task is due once per period, never twice). */
    bucket = ((now_ms - (uint32_t)phase_ms) / (uint32_t)period_ms) *
             (uint32_t)period_ms + (uint32_t)phase_ms;
    return bucket;
}

int16_t sched_register(sched_task_t *task)
{
    if ((task == SCHED_SLOT_EMPTY) || (task->name == NULL) ||
        (task->tick == NULL))
    {
        return -1;
    }
    if ((task->period_ms != 0U) && (task->phase_ms >= task->period_ms))
    {
        return -1;                         /* phase must be < period */
    }
    if (sched_used >= SCHED_MAX_TASKS)
    {
        return -1;
    }

    task->next_due_ms = (task->period_ms != 0U) ? (uint32_t)task->phase_ms
                                                : 0U;
    /* First due boundary = the phase itself: a phased task must not fire
       before its phase elapses, while a phase-0 task is due immediately. */
    task->run_count = 0U;
    task->max_late_ms = 0;
    sched_table[sched_used] = task;
    sched_used++;
    return (int16_t)(sched_used - 1U);
}

void sched_run_due(uint32_t now_ms)
{
    uint16_t i;

    for (i = 0U; i < sched_used; i++)
    {
        sched_task_t *t = sched_table[i];

        if (t->period_ms == 0U)
        {
            t->tick(now_ms);
            t->run_count++;
            continue;
        }

        /* Due when now reached the pending bucket (first call: next_due=0). */
        if ((int32_t)(now_ms - t->next_due_ms) >= 0)
        {
            int32_t late = (int32_t)(now_ms - t->next_due_ms);

            if (late > t->max_late_ms)
            {
                t->max_late_ms = late;
            }
            t->tick(now_ms);
            t->run_count++;

            /* Resynchronise to the first boundary strictly after now:
               no catch-up backlog after a long stall. */
            t->next_due_ms = sched_due_bucket(t->period_ms, t->phase_ms,
                                              now_ms);
            if ((uint32_t)(t->next_due_ms - now_ms) == 0U)
            {
                t->next_due_ms = now_ms + (uint32_t)t->period_ms;
            }
            else
            {
                t->next_due_ms += (uint32_t)t->period_ms;
            }
        }
    }
}

uint16_t sched_task_count(void)
{
    return sched_used;
}

bool sched_task_late(const sched_task_t *task, uint32_t now_ms, uint16_t tol_ms)
{
    if ((task == SCHED_SLOT_EMPTY) || (task->period_ms == 0U))
    {
        return false;
    }
    if (task->run_count == 0U)
    {
        /* Has never run: late once period + tolerance has passed. */
        return (now_ms > ((uint32_t)task->period_ms + (uint32_t)tol_ms));
    }
    return (task->max_late_ms > (int32_t)tol_ms);
}
