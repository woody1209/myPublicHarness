//#############################################################################
//
// FILE:   fsi_loop_hw.c
//
// TITLE:  FSI TX->RX internal loopback demo (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "fsi_loop_hw.h"
#include "frame_gen.h"
#include "board_pins.h"
#include "driverlib.h"

#define FSI_FRAME_WORDS   FRAME_GEN_WORDS
#define FSI_PRESCALAR     2U            /* slowest reasonable bit clock */

static uint16_t fsi_tx[FSI_FRAME_WORDS];
static uint16_t fsi_rx[FSI_FRAME_WORDS];
static bool fsi_inflight;

volatile uint32_t g_fsi_ok = 0U;
volatile uint32_t g_fsi_err = 0U;

static void fsi_flush_rx(void)
{
    FSI_clearRxEvents(FSIRXA_BASE, FSI_RX_EVT_FRAME_DONE | FSI_RX_EVT_DATA_FRAME);
}

bool fsi_loop_init(void)
{
    /* pins registered (optional jumper path); loopback does not need them */
    GPIO_setPadConfig(BOARD_FSITX_GPIO_D0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_FSITX_PINCFG_D0);
    GPIO_setPadConfig(BOARD_FSITX_GPIO_CLK, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_FSITX_PINCFG_CLK);
    GPIO_setPadConfig(BOARD_FSIRX_GPIO_D0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_FSIRX_PINCFG_D0);
    GPIO_setPadConfig(BOARD_FSIRX_GPIO_CLK, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_FSIRX_PINCFG_CLK);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_FSITXA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_FSIRXA);

    FSI_resetTxModule(FSITXA_BASE, FSI_TX_MAIN_CORE_RESET);
    FSI_resetRxModule(FSIRXA_BASE, FSI_RX_MAIN_CORE_RESET);
    FSI_configPrescalar(FSITXA_BASE, FSI_PRESCALAR);

    /* TX: single-lane, no CRC table, software frame start */
    FSI_enableTxClock(FSITXA_BASE);
    FSI_setTxFrameType(FSITXA_BASE, FSI_FRAME_TYPE_NWORD_DATA);
    FSI_setTxSoftwareFrameSize(FSITXA_BASE, FSI_FRAME_WORDS - 1U);
    FSI_setTxFrameTag(FSITXA_BASE, 0x5U);

    /* RX: internal loopback + tag match, 8 words */
    FSI_enableRxInternalLoopback(FSIRXA_BASE);
    FSI_setRxFrameTagRef(FSIRXA_BASE, 0x5U);
    FSI_setRxFrameTagMask(FSIRXA_BASE, 0x0U);
    FSI_setRxSoftwareFrameSize(FSIRXA_BASE, FSI_FRAME_WORDS - 1U);
    FSI_enableRxFrameTagCompare(FSIRXA_BASE);
    

    frame_gen_fill(fsi_tx, FRAME_GEN_SEED_2);
    fsi_inflight = false;
    return true;
}

void fsi_loop_step(void)
{
    if (fsi_inflight)
    {
        uint32_t st = FSI_getRxEventStatus(FSIRXA_BASE);
        bool done = (st & FSI_RX_EVT_FRAME_DONE) != 0U;
        bool bad = (st & (FSI_RX_EVT_CRC_ERR | FSI_RX_EVT_TYPE_ERR | FSI_RX_EVT_EOF_ERR)) != 0U;

        if (done && !bad)
        {
            FSI_readRxBuffer(FSIRXA_BASE, fsi_rx, FSI_FRAME_WORDS, 0U);
            if (frame_gen_verify(fsi_rx, FRAME_GEN_SEED_2))
            {
                g_fsi_ok++;
            }
            else
            {
                g_fsi_err++;
            }
            fsi_inflight = false;
        }
        else if (bad)
        {
            g_fsi_err++;
            fsi_inflight = false;
        }
        fsi_flush_rx();
    }
    else
    {
        FSI_writeTxBuffer(FSITXA_BASE, fsi_tx, FSI_FRAME_WORDS, 0U);
        FSI_startTxTransmit(FSITXA_BASE);
        fsi_inflight = true;
    }
}
