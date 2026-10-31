//#############################################################################
//
// FILE:   clb_demo_hw.c
//
// TITLE:  CLB tile register-interface demo (hardware layer).
//
// DESIGN (2026-10-25): GPREG is the one CLB register with no dependency on
//   tool-generated LUT/FSM/HLC configuration - write + readback proves the
//   tile clock, APB interface and GPREG block. Full HLC logic needs the
//   CLB Tool flow and stays pending in spec 0002 REQ-017.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "clb_demo_hw.h"
#include "driverlib.h"

static uint16_t clb_stamp;

volatile uint32_t g_clb_ok = 0U;
volatile uint32_t g_clb_err = 0U;

bool clb_demo_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CLB1);
    CLB_disableCLB(CLB1_BASE);          /* hold tile in reset for config */
    clb_stamp = 0U;
    return true;
}

//! GPREG write/readback: stamp a walking pattern through the tile APB port.
void clb_demo_step(void)
{
    uint32_t rd;

    clb_stamp = (uint16_t)(clb_stamp + 0x1111U);
    CLB_setGPREG(CLB1_BASE, (uint32_t)clb_stamp);
    rd = HWREG(CLB1_BASE + CLB_LOGICCTL + CLB_O_GP_REG) & 0xFFFFU;
    if (rd == clb_stamp) { g_clb_ok++; }
    else { g_clb_err++; }
}
