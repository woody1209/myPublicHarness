//#############################################################################
//
// FILE:   health.h
//
// TITLE:  Subsystem health monitor (pure logic, hardware independent).
//
// DESCRIPTION:
//   Every subsystem registers one monitor slot. Its periodic task calls
//   health_beat(id) on success and health_fail(id) on any mismatch/timeout.
//   The aggregated state (bit map + totals) drives:
//   - the 5 s console health line (REQ-020),
//   - the HIL H7 judgement (REQ-021),
//   - the fail-safe decision for "must" subsystems (REQ-001/020).
//
//   must-subsystems get a small consecutive-error threshold that latches the
//   fail-safe request; should/could subsystems only increment counters (they
//   degrade, they never take the platform down).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_HEALTH_H
#define FULLPLATFORM_HEALTH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//! Maximum monitored subsystems (keep <= 32: the map is a uint32_t bit map).
#define HEALTH_MAX_MONITORS           32U

//! Default consecutive-error threshold for "must" subsystems.
#define HEALTH_ERR_THRESHOLD_MUST     3U

//! Severity of a monitored subsystem.
typedef enum
{
    HEALTH_SEVERITY_MUST   = 0U,   //!< failure latches fail-safe
    HEALTH_SEVERITY_SHOULD = 1U,   //!< failure only counted
    HEALTH_SEVERITY_COULD  = 2U    //!< failure only counted
} health_severity_t;

typedef struct
{
    const char        *name;         //!< short id (<=7 chars)
    health_severity_t  severity;
    uint16_t           err_threshold;//!< consecutive errors -> fail-safe
    /* runtime state, maintained by the monitor: */
    uint32_t           beats;        //!< successful ticks
    uint32_t           errors;       //!< total failures
    uint16_t           consec_err;   //!< consecutive failures (unrecovered)
} health_mon_t;

//! Reset every monitor slot. Call once before registering.
void health_init(void);

//! Register a subsystem monitor. \return slot id (>=0) or -1 on error
//! (NULL name, table full). Threshold defaults per severity.
int16_t health_register(const char *name, health_severity_t severity);

//! Record a successful tick of subsystem \p id (clears the consecutive count).
void health_beat(int16_t id);

//! Record a failure of subsystem \p id (increments counters).
void health_fail(int16_t id);

//! \return bit map: bit i set when monitor i has errors > 0.
uint32_t health_map(void);

//! \return total failure count across all monitors.
uint32_t health_err_total(void);

//! \return smallest beat count across all MUST monitors (0 when none).
uint32_t health_min_beats(void);

//! \return id of the monitor with the most consecutive errors (-1 if none).
int16_t health_worst(void);

//! \return true when any MUST monitor reached its consecutive-error threshold.
bool health_should_failsafe(void);

//! \return name of monitor \p id, or NULL.
const char *health_name(int16_t id);

//! \return number of registered monitors.
uint16_t health_count(void);

#endif // FULLPLATFORM_HEALTH_H
