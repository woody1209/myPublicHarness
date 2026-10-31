//#############################################################################
//
// FILE:   spi_loop_hw.c
//
// TITLE:  SPI A/B digital-loopback transfers (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "spi_loop_hw.h"
#include "frame_gen.h"
#include "board_pins.h"
#include "driverlib.h"

#define SPI_CHARLEN      8U
#define SPI_TX_DELAY     10U

static uint16_t spi_a_tx[FRAME_GEN_WORDS];
static uint16_t spi_a_rx[FRAME_GEN_WORDS];
static uint16_t spi_a_tx_idx;
static uint16_t spi_a_rx_idx;
static uint16_t spi_b_tx[FRAME_GEN_WORDS];
static uint16_t spi_b_rx[FRAME_GEN_WORDS];
static bool spi_b_pending;

volatile uint32_t g_spi_a_ok = 0U;
volatile uint32_t g_spi_a_err = 0U;
volatile uint32_t g_spi_b_done = 0U;

bool spi_loop_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SPIA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SPIB);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SPIA);  /* (idempotent) */

    /* pins registered for probing; loopback does not depend on them */
    GPIO_setPadConfig(BOARD_SPIA_GPIO_SIMO, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIA_PINCFG_SIMO);
    GPIO_setPadConfig(BOARD_SPIA_GPIO_SOMI, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIA_PINCFG_SOMI);
    GPIO_setPadConfig(BOARD_SPIA_GPIO_CLK, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIA_PINCFG_CLK);
    GPIO_setPadConfig(BOARD_SPIB_GPIO_SIMO, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIB_PINCFG_SIMO);
    GPIO_setPadConfig(BOARD_SPIB_GPIO_SOMI, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIB_PINCFG_SOMI);
    GPIO_setPadConfig(BOARD_SPIB_GPIO_CLK, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SPIB_PINCFG_CLK);

    SPI_enableLoopback(SPIA_BASE);
    SPI_setConfig(SPIA_BASE, BOARD_SCI_LSPCLK_HZ, SPI_CHARLEN,
                  SPI_MODE_MASTER, 500000U, SPI_CHARLEN);
    SPI_enableModule(SPIA_BASE);
    SPI_enableTalk(SPIA_BASE);
    /* FIFO enable LAST: an earlier order left SPIFFENA clear on silicon
     * (2026-10-25 register dump) and the loopback starved. */
    SPI_enableFIFO(SPIA_BASE);
    SPI_resetTxFIFO(SPIA_BASE);
    SPI_resetRxFIFO(SPIA_BASE);
    frame_gen_fill(spi_a_tx, FRAME_GEN_SEED_1);   /* first block is real */

    SPI_enableLoopback(SPIB_BASE);
    SPI_setConfig(SPIB_BASE, BOARD_SCI_LSPCLK_HZ, SPI_CHARLEN,
                  SPI_MODE_MASTER, 500000U, SPI_CHARLEN);
    SPI_enableModule(SPIB_BASE);
    SPI_enableTalk(SPIB_BASE);
    SPI_enableFIFO(SPIB_BASE);
    SPI_resetTxFIFO(SPIB_BASE);
    SPI_resetRxFIFO(SPIB_BASE);

    return true;
}

//! Byte-per-call state machine: the driverlib polling helper busy-waits
//! with NO timeout and would hang the whole scheduler if the SPI clock
//! ever stalls (2026-10-25 first M3 boot: LED frozen, all slots > spi
//! starved). Every wait here is one bounded FIFO check per 10 ms step.
void spi_loop_step_a(void)
{
    if (spi_a_tx_idx < FRAME_GEN_WORDS)
    {
        if (SPI_getTxFIFOStatus(SPIA_BASE) < SPI_FIFO_TX16)
        {
            SPI_writeDataNonBlocking(SPIA_BASE, spi_a_tx[spi_a_tx_idx++]);
        }
    }
    else if (spi_a_rx_idx < FRAME_GEN_WORDS)
    {
        if (SPI_getRxFIFOStatus(SPIA_BASE) > SPI_FIFO_RX0)
        {
            spi_a_rx[spi_a_rx_idx++] =
                SPI_readDataNonBlocking(SPIA_BASE) & 0xFFFFU;
        }
    }
    else
    {
        if (frame_gen_verify(spi_a_rx, FRAME_GEN_SEED_1))
        {
            g_spi_a_ok++;
        }
        else
        {
            g_spi_a_err++;
        }
        /* restart the block with a fresh pattern */
        frame_gen_fill(spi_a_tx, FRAME_GEN_SEED_1);
        spi_a_tx_idx = 0U;
        spi_a_rx_idx = 0U;
    }
}

void spi_loop_start_b(void)
{
    if (spi_b_pending) { return; }
    frame_gen_fill(spi_b_tx, FRAME_GEN_SEED_2);
    frame_gen_fill(spi_b_rx, 0x0000U);        /* poison the expect side */
    spi_b_pending = true;
}

bool spi_loop_verify_b(void)
{
    if (!spi_b_pending) { return true; }
    if (!frame_gen_verify(spi_b_rx, FRAME_GEN_SEED_2))
    {
        return false;                          /* DMA transfer not done yet */
    }
    spi_b_pending = false;
    g_spi_b_done++;
    return true;
}

uint16_t *spi_loop_b_tx_buf(void) { return spi_b_tx; }
uint16_t *spi_loop_b_rx_buf(void) { return spi_b_rx; }
