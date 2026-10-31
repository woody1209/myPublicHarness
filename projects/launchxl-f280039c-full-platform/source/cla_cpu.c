//#############################################################################
//
// FILE:   cla_cpu.c
//
// TITLE:  CPU side of the CLA statistics task (hardware layer, REQ-004).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "cla_app_shared.h"
#include "frame_gen.h"
#include <string.h>
#include "driverlib.h"

#if !defined(CLA_C)

volatile uint32_t g_cla_ok = 0U;
volatile uint32_t g_cla_err = 0U;
volatile uint32_t g_cla_runs = 0U;

static uint32_t cla_ref_sum;
static uint32_t cla_ref_min;
static uint32_t cla_ref_max;
static bool cla_pending;

bool cla_cpu_init(void)
{
    uint16_t i;
    uint32_t s = 0U;
    uint32_t mn = 0xFFFFFFFFU;
    uint32_t mx = 0U;

    extern uint16_t Cla1ProgRunStart;
    extern uint16_t Cla1ProgLoadStart;
    extern uint16_t Cla1ProgLoadSize;

    /* ADR 0003 class: _c_int00 does NOT copy LOAD->RUN sections - the CLA
     * program must be memcpy'd from flash to LS1 BEFORE the task vector is
     * ever forced, or the CLA executes uninitialised RAM, corrupts shared
     * RAM and the watchdog false-resets the platform (2026-10-25 M4 boot). */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CLA1);
    memcpy(&Cla1ProgRunStart, &Cla1ProgLoadStart, (uint32_t)&Cla1ProgLoadSize);
    /* LS1 mastered by the CLA as program RAM; MVEC maps task 1 */
    MemCfg_setLSRAMControllerSel(MEMCFG_SECT_LS1,
                                 MEMCFG_LSRAMCONTROLLER_CPU_CLA1);
    MemCfg_setCLAMemType(MEMCFG_SECT_LS1,
                         MEMCFG_CLA_MEM_PROGRAM);
    CLA_mapTaskVector(CLA1_BASE, CLA_MVECT_1, (uint16_t)Cla1ProgRunStart);
    CLA_enableTasks(CLA1_BASE, CLA_TASKFLAG_1);

    cla_ref_sum = 0U;
    cla_ref_min = 0U;
    cla_ref_max = 0U;
    cla_pending = false;
    (void)s; (void)mn; (void)mx;
    (void)i;
    return true;
}

//! CPU prepares one input block and forces Task 1 (100 ms step).
void cla_cpu_step(void)
{
    uint16_t i;

    if (cla_pending)
    {
        return;                             /* previous task not done */
    }
    frame_gen_fill((uint16_t *)cla_in, FRAME_GEN_SEED_1);
    {
        uint32_t s = 0U;
        uint32_t mn = 0xFFFFFFFFU;
        uint32_t mx = 0U;

        for (i = 0U; i < CLA_N_IN; i++)
        {
            uint32_t v = ((uint32_t)cla_in[2 * i] << 16) | cla_in[2 * i + 1U];

            s += v;
            if (v < mn) { mn = v; }
            if (v > mx) { mx = v; }
        }
        cla_ref_sum = s;
        cla_ref_min = mn;
        cla_ref_max = mx;
    }
    /* CLA task force gated: bring-up open (program-copy verified but task
     * execution still reset-loops the platform on 2026-10-25; see spec
     * 0002 REQ-004 - counters stay 0 until this closes). */
    cla_pending = true;
}

//! Judge the CLA result against the CPU reference (call every 100 ms too).
void cla_cpu_judge(void)
{
    if (!cla_pending)
    {
        return;
    }
    if (cla_done == 0xA5A5U)
    {
        uint32_t v[CLA_N_IN];
        uint32_t s = 0U;
        uint32_t mn = 0xFFFFFFFFU;
        uint32_t mx = 0U;
        uint16_t i;

        for (i = 0U; i < CLA_N_IN; i++)
        {
            v[i] = ((uint32_t)cla_in[2 * i] << 16) | cla_in[2 * i + 1U];
            s += v[i];
            if (v[i] < mn) { mn = v[i]; }
            if (v[i] > mx) { mx = v[i]; }
        }
        g_cla_runs++;
        if ((cla_sum == s) && (cla_min == mn) && (cla_max == mx))
        {
            g_cla_ok++;
        }
        else
        {
            g_cla_err++;
        }
        cla_pending = false;
    }
}

#endif /* !CLA_C */
