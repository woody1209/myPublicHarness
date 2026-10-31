//#############################################################################
//
// FILE:   i2c_pair_hw.c
//
// TITLE:  I2CA controller loopback demo (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "i2c_pair_hw.h"
#include "frame_gen.h"
#include "board_pins.h"
#include "driverlib.h"

#define I2C_TARGET_ADDR     0x3CU       /* self-address in loopback */
#define I2C_PASS_TIMEOUT    2000U       /* status-poll budget per word: 16x2
                                         * guards must fit the watchdog
                                         * window (~838 ms) inside one pass */

static uint16_t i2c_tx[FRAME_GEN_WORDS];
static uint16_t i2c_rx[FRAME_GEN_WORDS];
static bool i2c_phase_write;            /* true = in write phase */

volatile uint32_t g_i2c_ok = 0U;
volatile uint32_t g_i2c_err = 0U;

bool i2c_pair_init(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_I2CA);

    GPIO_setPadConfig(BOARD_I2CA_GPIO_SDA, GPIO_PIN_TYPE_OD);
    GPIO_setPinConfig(BOARD_I2CA_PINCFG_SDA);
    GPIO_setPadConfig(BOARD_I2CA_GPIO_SCL, GPIO_PIN_TYPE_OD);
    GPIO_setPinConfig(BOARD_I2CA_PINCFG_SCL);

    I2C_disableModule(I2CA_BASE);
    I2C_initController(I2CA_BASE, 120000000U, 100000U, I2C_DUTYCYCLE_50);
    I2C_setSlaveAddress(I2CA_BASE, I2C_TARGET_ADDR);
    I2C_setDataCount(I2CA_BASE, FRAME_GEN_WORDS);
    I2C_enableLoopback(I2CA_BASE);
    I2C_enableFIFO(I2CA_BASE);
    I2C_enableModule(I2CA_BASE);

    i2c_phase_write = true;
    return true;
}

//! Drive one pass: write phase then read phase, each polling I2C status
//! with a bounded budget (no infinite waits in the scheduler).
void i2c_pair_step(void)
{
    uint16_t i;
    uint32_t guard;

    if (i2c_phase_write)
    {
        frame_gen_fill(i2c_tx, FRAME_GEN_SEED_2);
        I2C_setConfig(I2CA_BASE, I2C_CONTROLLER_SEND_MODE);
        I2C_setDataCount(I2CA_BASE, FRAME_GEN_WORDS);
        I2C_sendStartCondition(I2CA_BASE);
        I2C_sendStopCondition(I2CA_BASE);
        for (i = 0U; i < FRAME_GEN_WORDS; i++)
        {
            guard = 0U;
            while (((I2C_getTxFIFOStatus(I2CA_BASE) == I2C_FIFO_TXFULL) ||
                    ((I2C_getStatus(I2CA_BASE) & I2C_STS_TX_DATA_RDY) == 0U)) &&
                   (guard < I2C_PASS_TIMEOUT))
            {
                guard++;
            }
            I2C_putData(I2CA_BASE, i2c_tx[i]);
        }
        i2c_phase_write = false;
    }
    else
    {
        I2C_setConfig(I2CA_BASE, I2C_CONTROLLER_RECEIVE_MODE);
        I2C_setDataCount(I2CA_BASE, FRAME_GEN_WORDS);
        I2C_sendStartCondition(I2CA_BASE);
        I2C_sendStopCondition(I2CA_BASE);
        for (i = 0U; i < FRAME_GEN_WORDS; i++)
        {
            guard = 0U;
            while ((I2C_getRxFIFOStatus(I2CA_BASE) == I2C_FIFO_RXEMPTY) &&
                   (guard < I2C_PASS_TIMEOUT))
            {
                guard++;
            }
            i2c_rx[i] = I2C_getData(I2CA_BASE);
        }
        {
            bool ok = true;
            for (i = 0U; i < FRAME_GEN_WORDS; i++)
            {
                if (i2c_rx[i] != i2c_tx[i]) { ok = false; }
            }
            if (ok) { g_i2c_ok++; }
            else { g_i2c_err++; }
        }
        i2c_phase_write = true;
    }
}
