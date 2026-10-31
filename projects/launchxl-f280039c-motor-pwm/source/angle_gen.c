//#############################################################################
//
// FILE:   angle_gen.c
//
// TITLE:  Open-loop electrical angle generator (PURE LOGIC, host-testable).
//
// DESCRIPTION:
//   Exact rational accumulator: one 0.1-degree step corresponds to
//   100000000 / (rate_hz_x100) microseconds of elapsed time, so the carry
//   'accum' holds the sub-step remainder in units of
//   (0.1 degree * microsecond / rate_hz_x100). Splitting one elapsed time
//   into several ticks therefore reaches the SAME final angle as one long
//   tick (associativity, asserted by the host tests) - no step is lost.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "angle_gen.h"

//
// Units per single 0.1-degree step at rate r (0.01 Hz):
//   time per 0.1 deg = 1 / (r/100 * 3600 * 10) s = 100 / (r * 36000) s
//                   = 1e8 / (r * 3600) us
// With accum in units of (r * us) this constant is 1e8 / 3600 -> keep the
// division out of the hot path by pre-scaling: one step per
// ANGLE_STEP_UNITS = 100000000 / 3600 accum units.
//
#define ANGLE_STEP_UNITS        100000000ULL
#define ANGLE_STEP_DIV          3600ULL

void angle_gen_init(angle_gen_t *g, uint32_t rate_hz_x100)
{
    if (g == 0)
    {
        return;
    }
    g->angle_x10 = 0U;
    g->rate_hz_x100 = (rate_hz_x100 > ANGLE_GEN_RATE_MAX_X100)
                          ? ANGLE_GEN_RATE_MAX_X100 : rate_hz_x100;
    g->accum = 0ULL;
}

uint32_t angle_gen_advance(angle_gen_t *g, uint32_t tick_us)
{
    if ((g == 0) || (tick_us == 0U) || (g->rate_hz_x100 == 0U))
    {
        return (g == 0) ? 0U : g->angle_x10;
    }

    g->accum += (uint64_t)g->rate_hz_x100 * (uint64_t)tick_us * ANGLE_STEP_DIV;

    //
    // O(1) step extraction: one division, no loop. 'steps' is the number of
    // 0.1-degree steps the accumulated time covers; the remainder stays in
    // accum so the next call continues exactly where this one stopped.
    //
    {
        uint64_t steps = g->accum / ANGLE_STEP_UNITS;
        g->accum -= steps * ANGLE_STEP_UNITS;
        g->angle_x10 = (uint32_t)(((uint64_t)g->angle_x10 + steps) %
                                  (uint64_t)ANGLE_GEN_FULL_SCALE_X10);
    }
    return g->angle_x10;
}

int angle_gen_set_rate_hz_x100(angle_gen_t *g, uint32_t rate_hz_x100)
{
    if (g == 0)
    {
        return -1;
    }
    if (rate_hz_x100 > ANGLE_GEN_RATE_MAX_X100)
    {
        return -1;                  // keep the previous rate
    }
    g->rate_hz_x100 = rate_hz_x100;
    return 0;
}

uint32_t angle_gen_get(const angle_gen_t *g)
{
    return (g == 0) ? 0U : g->angle_x10;
}
