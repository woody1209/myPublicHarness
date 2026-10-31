//#############################################################################
//
// FILE:   can_loop_hw.c
//
// TITLE:  MCANA classic-CAN self-test loopback (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "can_loop_hw.h"
#include "frame_gen.h"
#include "board_pins.h"
#include "driverlib.h"

#define CAN_MSG_LEN    8U
#define CAN_OBJ_STD_TX 1U
#define CAN_OBJ_STD_RX 2U
#define CAN_OBJ_EXT_TX 3U
#define CAN_OBJ_EXT_RX 4U
#define CAN_STD_ID     1U
#define CAN_EXT_ID     0x1ABCDEU

static uint16_t can_tx[CAN_MSG_LEN];
static uint16_t can_rx[CAN_MSG_LEN];
static bool can_std_phase;

volatile uint32_t g_can_ok = 0U;
volatile uint32_t g_can_err = 0U;

bool can_loop_init(void)
{
    GPIO_setPadConfig(BOARD_MCAN_GPIO_TX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_MCAN_PINCFG_TX);
    GPIO_setPadConfig(BOARD_MCAN_GPIO_RX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_MCAN_PINCFG_RX);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CANA);
    /* The CAN clock defaults to XTAL - the LaunchPad has NO crystal
     * (ADR 0001: INTOSC2). A dead CAN clock leaves IF1CMD.BUSY stuck and
     * CAN_sendMessage's unbounded busy-wait hangs the whole scheduler
     * (2026-10-25 night bring-up). Select SYSCLK explicitly. */
    CAN_selectClockSource(CANA_BASE, CAN_CLOCK_SOURCE_SYS);
    CAN_initModule(CANA_BASE);
    CAN_setBitRate(CANA_BASE, 120000000U, 500000U, 16U);

    /* TX objects (auto-answer not needed: loopback feeds RX directly) */
    CAN_setupMessageObject(CANA_BASE, CAN_OBJ_STD_TX, CAN_STD_ID,
                           CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_TX,
                           0U, CAN_MSG_OBJ_NO_FLAGS, CAN_MSG_LEN);
    CAN_setupMessageObject(CANA_BASE, CAN_OBJ_EXT_TX, CAN_EXT_ID,
                           CAN_MSG_FRAME_EXT, CAN_MSG_OBJ_TYPE_TX,
                           0U, CAN_MSG_OBJ_NO_FLAGS, CAN_MSG_LEN);
    /* RX objects accept the loopback frames */
    CAN_setupMessageObject(CANA_BASE, CAN_OBJ_STD_RX, CAN_STD_ID,
                           CAN_MSG_FRAME_STD, CAN_MSG_OBJ_TYPE_RX,
                           0U, CAN_MSG_OBJ_NO_FLAGS, CAN_MSG_LEN);
    CAN_setupMessageObject(CANA_BASE, CAN_OBJ_EXT_RX, CAN_EXT_ID,
                           CAN_MSG_FRAME_EXT, CAN_MSG_OBJ_TYPE_RX,
                           0U, CAN_MSG_OBJ_NO_FLAGS, CAN_MSG_LEN);

    /* self-test loopback: TX feeds RX internally, no transceiver needed */
    CAN_enableTestMode(CANA_BASE, CAN_TEST_LBACK);
    CAN_enableController(CANA_BASE);

    can_std_phase = true;
    frame_gen_fill(can_tx, FRAME_GEN_SEED_1);
    return true;
}

void can_loop_step(void)
{
    uint32_t obj_tx;
    uint32_t obj_rx;
    uint32_t guard;
    bool got;

    if (can_std_phase)
    {
        obj_tx = CAN_OBJ_STD_TX;
        obj_rx = CAN_OBJ_STD_RX;
    }
    else
    {
        obj_tx = CAN_OBJ_EXT_TX;
        obj_rx = CAN_OBJ_EXT_RX;
    }

    (void)CAN_sendMessage(CANA_BASE, obj_tx, CAN_MSG_LEN, can_tx);

    guard = 40000U;
    got = false;
    while ((guard > 0U) && !got)
    {
        if (CAN_getNewDataFlags(CANA_BASE) & (1UL << obj_rx))
        {
            got = CAN_readMessage(CANA_BASE, obj_rx, can_rx);
        }
        guard--;
    }

    if (got)
    {
        uint16_t i;
        bool ok = true;

        for (i = 0U; i < CAN_MSG_LEN; i++)
        {
            if (can_rx[i] != can_tx[i]) { ok = false; }
        }
        if (ok) { g_can_ok++; }
        else { g_can_err++; }
    }
    else
    {
        g_can_err++;
    }

    can_std_phase = !can_std_phase;
}
