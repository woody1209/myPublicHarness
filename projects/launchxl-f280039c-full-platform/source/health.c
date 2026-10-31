//#############################################################################
//
// FILE:   health.c
//
// TITLE:  Subsystem health monitor (pure logic).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "health.h"

//! Monitor state lives in a static pool; subsystems only supply name and
//! severity at registration time (no descriptor storage in caller modules).
/* NOT static: diag21 dumps it by symbol for board bring-up */
health_mon_t health_pool[HEALTH_MAX_MONITORS];
static uint16_t health_used;

void health_init(void)
{
    uint16_t i;

    for (i = 0U; i < HEALTH_MAX_MONITORS; i++)
    {
        health_pool[i].name = NULL;
        health_pool[i].severity = HEALTH_SEVERITY_COULD;
        health_pool[i].err_threshold = 0U;
        health_pool[i].beats = 0U;
        health_pool[i].errors = 0U;
        health_pool[i].consec_err = 0U;
    }
    health_used = 0U;
}

int16_t health_register(const char *name, health_severity_t severity)
{
    health_mon_t *mon;

    if ((name == NULL) || (health_used >= HEALTH_MAX_MONITORS))
    {
        return -1;
    }

    mon = &health_pool[health_used];
    mon->name = name;
    mon->severity = severity;
    /* MUST subsystems latch fail-safe after a few consecutive errors;
       SHOULD/COULD subsystems never latch (degrade only). */
    mon->err_threshold = (severity == HEALTH_SEVERITY_MUST)
                             ? HEALTH_ERR_THRESHOLD_MUST
                             : UINT16_MAX;
    mon->beats = 0U;
    mon->errors = 0U;
    mon->consec_err = 0U;

    health_used++;
    return (int16_t)(health_used - 1U);
}

void health_beat(int16_t id)
{
    if ((id < 0) || ((uint16_t)id >= health_used))
    {
        return;
    }
    health_pool[id].beats++;
    health_pool[id].consec_err = 0U;
}

void health_fail(int16_t id)
{
    if ((id < 0) || ((uint16_t)id >= health_used))
    {
        return;
    }
    health_pool[id].errors++;
    if (health_pool[id].consec_err < UINT16_MAX)
    {
        health_pool[id].consec_err++;
    }
}

uint32_t health_map(void)
{
    uint32_t map = 0U;
    uint16_t i;

    for (i = 0U; i < health_used; i++)
    {
        if (health_pool[i].errors > 0U)
        {
            map |= (1UL << i);
        }
    }
    return map;
}

uint32_t health_err_total(void)
{
    uint32_t total = 0U;
    uint16_t i;

    for (i = 0U; i < health_used; i++)
    {
        total += health_pool[i].errors;
    }
    return total;
}

uint32_t health_min_beats(void)
{
    uint32_t min_beats = UINT32_MAX;
    uint16_t i;

    for (i = 0U; i < health_used; i++)
    {
        if ((health_pool[i].severity == HEALTH_SEVERITY_MUST) &&
            (health_pool[i].beats < min_beats))
        {
            min_beats = health_pool[i].beats;
        }
    }
    return (min_beats == UINT32_MAX) ? 0U : min_beats;
}

int16_t health_worst(void)
{
    int16_t worst = -1;
    uint16_t worst_err = 0U;
    uint16_t i;

    for (i = 0U; i < health_used; i++)
    {
        if (health_pool[i].consec_err > worst_err)
        {
            worst_err = health_pool[i].consec_err;
            worst = (int16_t)i;
        }
    }
    return worst;
}

bool health_should_failsafe(void)
{
    uint16_t i;

    for (i = 0U; i < health_used; i++)
    {
        if ((health_pool[i].severity == HEALTH_SEVERITY_MUST) &&
            (health_pool[i].consec_err >= health_pool[i].err_threshold))
        {
            return true;
        }
    }
    return false;
}

const char *health_name(int16_t id)
{
    if ((id < 0) || ((uint16_t)id >= health_used))
    {
        return NULL;
    }
    return health_pool[id].name;
}

uint16_t health_count(void)
{
    return health_used;
}
