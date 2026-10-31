//#############################################################################
//
// FILE:   qep_quadv_hw.c
//
// TITLE:  eQEP 1/2 quadrature pad-loopback (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "qep_quadv_hw.h"
#include "board_pins.h"
#include "driverlib.h"

#define QEP_RATE_TOL_PCT   10U
#define QEP_TASK_PERIOD_MS 100U

volatile int32_t g_qep_pos[2] = {0, 0};
const uint32_t g_qep_rate_hz[2] = {4000U, 20000U};  /* 4 edges * 1k/5k Hz */
volatile uint32_t g_qep_rate_meas[2] = {0U, 0U};
volatile bool g_qep_forward[2] = {true, true};
volatile uint32_t g_qep_ok_mask = 0U;

static const uint32_t qep_base[2] = { EQEP1_BASE, EQEP2_BASE };
static int32_t qep_last_pos[2];
static uint32_t qep_ok_bits;

static bool qep_init_one(uint32_t base)
{
    EQEP_setDecoderConfig(base, EQEP_CONFIG_QUADRATURE |
                                EQEP_CONFIG_2X_RESOLUTION |
                                EQEP_CONFIG_NO_SWAP);
    EQEP_setPositionCounterConfig(base, EQEP_POSITION_RESET_MAX_POS,
                                  0xFFFFFFFFU);
    EQEP_setEmulationMode(base, EQEP_EMULATIONMODE_RUNFREE);
    EQEP_setSWPositionInit(base, false);
    EQEP_enableModule(base);
    return true;
}

bool qep_quadv_init(void)
{
    uint32_t i;
    bool ok = true;

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EQEP2);

    /* quadrature GPIOs -> Input X-BAR 1-4 -> EPWM X-BAR trips -> eQEP source */
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_QEP1A, BOARD_EPWM4A_GPIO);
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_QEP1B, BOARD_EPWM4B_GPIO);
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_QEP2A, BOARD_EPWM8A_GPIO);
    XBAR_setInputPin(INPUTXBAR_BASE, BOARD_INPUT_QEP2B, BOARD_EPWM8B_GPIO);

    XBAR_setEPWMMuxConfig(BOARD_TRIP_QEP1A, XBAR_EPWM_MUX01_INPUTXBAR1);
    XBAR_enableEPWMMux(BOARD_TRIP_QEP1A, XBAR_MUX01);
    XBAR_setEPWMMuxConfig(BOARD_TRIP_QEP1B, XBAR_EPWM_MUX03_INPUTXBAR2);
    XBAR_enableEPWMMux(BOARD_TRIP_QEP1B, XBAR_MUX03);
    XBAR_setEPWMMuxConfig(BOARD_TRIP_QEP2A, XBAR_EPWM_MUX05_INPUTXBAR3);
    XBAR_enableEPWMMux(BOARD_TRIP_QEP2A, XBAR_MUX05);
    XBAR_setEPWMMuxConfig(BOARD_TRIP_QEP2B, XBAR_EPWM_MUX07_INPUTXBAR4);
    XBAR_enableEPWMMux(BOARD_TRIP_QEP2B, XBAR_MUX07);

    {
        EQEP_SourceSelect src1;
        EQEP_SourceSelect src2;

        src1.sourceA = BOARD_QEP1A_SOURCE;
        src1.sourceB = BOARD_QEP1B_SOURCE;
        src1.sourceIndex = EQEP_SOURCE_ZERO;
        EQEP_selectSource(EQEP1_BASE, src1);
        src2.sourceA = BOARD_QEP2A_SOURCE;
        src2.sourceB = BOARD_QEP2B_SOURCE;
        src2.sourceIndex = EQEP_SOURCE_ZERO;
        EQEP_selectSource(EQEP2_BASE, src2);
    }

    for (i = 0U; i < 2U; i++)
    {
        qep_last_pos[i] = 0;
        if (!qep_init_one(qep_base[i])) { ok = false; }
    }
    return ok;
}

void qep_quadv_step(void)
{
    uint32_t i;

    for (i = 0U; i < 2U; i++)
    {
        int32_t pos = (int32_t)EQEP_getPosition(qep_base[i]);
        int32_t delta = pos - qep_last_pos[i];
        uint32_t rate;

        g_qep_pos[i] = pos;
        g_qep_forward[i] = (EQEP_getStatus(qep_base[i]) &
                            EQEP_STS_DIR_FLAG) != 0U;

        if (delta != 0)
        {
            uint32_t bit = (1UL << i);
            rate = (uint32_t)((delta < 0) ? -delta : delta);
            rate *= (1000U / QEP_TASK_PERIOD_MS);
            g_qep_rate_meas[i] = rate;

            /* clear-on-fail: a channel that stops counting must unhook its
             * bit, not keep a stale pass from boot (same lesson as eCAP). */
            if ((rate * 100U) >=
                (g_qep_rate_hz[i] * (100U - QEP_RATE_TOL_PCT)))
            {
                if ((rate * 100U) <=
                    (g_qep_rate_hz[i] * (100U + QEP_RATE_TOL_PCT)))
                {
                    qep_ok_bits |= bit;
                }
                else
                {
                    qep_ok_bits &= ~bit;
                }
            }
            else
            {
                qep_ok_bits &= ~bit;
            }
            g_qep_ok_mask = qep_ok_bits;
        }
        qep_last_pos[i] = pos;
    }
}
