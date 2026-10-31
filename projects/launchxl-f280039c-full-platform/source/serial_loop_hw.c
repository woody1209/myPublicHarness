//#############################################################################
//
// FILE:   serial_loop_hw.c
//
// TITLE:  SCIB + LINA/LINB internal-loopback serial demo (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "serial_loop_hw.h"
#include "frame_gen.h"
#include "board_pins.h"
#include "driverlib.h"

#define SER_FRAME_BYTES   FRAME_GEN_WORDS   /* 8 bytes per exchange */

static uint16_t ser_tx[SER_FRAME_BYTES];
static uint16_t ser_rx[SER_FRAME_BYTES];
static uint16_t ser_slot;               /* 0 = SCIB, 1 = LINA, 2 = LINB */
static uint16_t ser_tx_idx;
static uint16_t ser_rx_idx;
static uint16_t ser_seed;

volatile uint32_t g_ser_ok = 0U;
volatile uint32_t g_ser_err = 0U;

static void ser_next_slot(void)
{
    ser_slot++;
    if (ser_slot >= 3U) { ser_slot = 0U; }
}

bool serial_loop_init(void)
{
    /* SCIB: plain SCI with internal loopback */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SCIB);
    GPIO_setPadConfig(BOARD_SCIB_GPIO_TX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SCIB_PINCFG_TX);
    GPIO_setPadConfig(BOARD_SCIB_GPIO_RX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_SCIB_PINCFG_RX);
    SCI_performSoftwareReset(SCIB_BASE);
    SCI_setConfig(SCIB_BASE, BOARD_SCI_LSPCLK_HZ, 115200U,
                  (SCI_CONFIG_WLEN_8 | SCI_CONFIG_PAR_NONE |
                   SCI_CONFIG_STOP_ONE));
    SCI_enableLoopback(SCIB_BASE);
    SCI_enableModule(SCIB_BASE);
    SCI_resetTxFIFO(SCIB_BASE);
    SCI_resetRxFIFO(SCIB_BASE);
    SCI_enableFIFO(SCIB_BASE);

    /* LINA/LINB: SCI-compatible mode with digital loopback */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_LINA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_LINB);
    GPIO_setPadConfig(BOARD_LINA_GPIO_TX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_LINA_PINCFG_TX);
    GPIO_setPadConfig(BOARD_LINA_GPIO_RX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_LINA_PINCFG_RX);
    GPIO_setPadConfig(BOARD_LINB_GPIO_TX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_LINB_PINCFG_TX);
    GPIO_setPadConfig(BOARD_LINB_GPIO_RX, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(BOARD_LINB_PINCFG_RX);

    LIN_disableModule(LINA_BASE);
    LIN_setBaudRatePrescaler(LINA_BASE, 390U, 10U);
    LIN_enableSCIMode(LINA_BASE);
    LIN_setSCICharLength(LINA_BASE, 8U);
    LIN_setSCIStopBits(LINA_BASE, LIN_SCI_STOP_ONE);
    LIN_enableIntLoopback(LINA_BASE);
    LIN_enableModule(LINA_BASE);
    LIN_enableSCIModuleErrors(LINA_BASE, 0xFFFFFFFFU);

    LIN_disableModule(LINB_BASE);
    LIN_setBaudRatePrescaler(LINB_BASE, 390U, 10U);
    LIN_enableSCIMode(LINB_BASE);
    LIN_setSCICharLength(LINB_BASE, 8U);
    LIN_setSCIStopBits(LINB_BASE, LIN_SCI_STOP_ONE);
    LIN_enableIntLoopback(LINB_BASE);
    LIN_enableModule(LINB_BASE);
    LIN_enableSCIModuleErrors(LINB_BASE, 0xFFFFFFFFU);

    ser_slot = 0U;
    ser_tx_idx = 0U;
    ser_rx_idx = 0U;
    ser_seed = FRAME_GEN_SEED_1;
    frame_gen_fill(ser_tx, ser_seed);
    return true;
}

//! Poll-driven byte exchange: feed TX while space, collect RX, verify at
//! frame end. The round-robin slot picks the module; each call services it.
void serial_loop_step(void)
{
    uint32_t base;
    bool is_lin;
    bool done;

    if (ser_slot == 0U) { base = SCIB_BASE; is_lin = false; }
    else if (ser_slot == 1U) { base = LINA_BASE; is_lin = true; }
    else { base = LINB_BASE; is_lin = true; }

    done = false;

    /* feed one byte per step (slow enough for any FIFO depth) */
    if (ser_tx_idx < SER_FRAME_BYTES)
    {
        if (!is_lin)
        {
            if (SCI_getTxFIFOStatus(base) < SCI_FIFO_TX16)
            {
                SCI_writeCharBlockingFIFO(base, ser_tx[ser_tx_idx++] & 0xFFU);
            }
        }
        else if (LIN_isSCISpaceAvailable(base))
        {
            LIN_writeSCICharNonBlocking(base, ser_tx[ser_tx_idx++] & 0xFFU);
        }
    }
    else
    {
        if (ser_rx_idx < SER_FRAME_BYTES)
        {
            if (!is_lin)
            {
                if (SCI_getRxFIFOStatus(base) > SCI_FIFO_RX0)
                {
                    ser_rx[ser_rx_idx++] = SCI_readCharBlockingFIFO(base);
                }
            }
            else if (LIN_isSCIDataAvailable(base))
            {
                ser_rx[ser_rx_idx++] = LIN_readSCICharNonBlocking(base, false);
            }
        }
        else
        {
            done = true;
        }
    }

    if (done)
    {
        bool ok = true;
        uint16_t i;

        for (i = 0U; i < SER_FRAME_BYTES; i++)
        {
            if (ser_rx[i] != (ser_tx[i] & 0xFFU)) { ok = false; }
        }
        if (ok) { g_ser_ok++; }
        else { g_ser_err++; }

        ser_tx_idx = 0U;
        ser_rx_idx = 0U;
        ser_next_slot();
    }
}
