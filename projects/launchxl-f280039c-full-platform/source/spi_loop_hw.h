//#############################################################################
//
// FILE:   spi_loop_hw.h
//
// TITLE:  SPI A/B digital-loopback transfers (hardware layer, REQ-011).
//
// DESCRIPTION:
//   Each SPI runs in digital loopback (SPI_enableLoopback): the TX FIFO
//   feeds the RX shift path inside the module. SPIA transfers 8-word
//   frame_gen blocks by polling; SPIB transfers through DMA channels 1/2
//   (dma_bank_hw) - the DMA-completed block is verified here. Byte-exact
//   comparison counts into the health monitor via the m3 status bits.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_SPI_LOOP_HW_H
#define FULLPLATFORM_SPI_LOOP_HW_H

#include <stdint.h>
#include <stdbool.h>

bool spi_loop_init(void);

//! SPIA polled 8-word loopback transaction (10 ms task step).
void spi_loop_step_a(void);

//! Kick one DMA-driven SPIB transaction (called by dma_bank at 1 s).
void spi_loop_start_b(void);

//! Verify the DMA-received SPIB block. \return true when byte-exact.
bool spi_loop_verify_b(void);

extern volatile uint32_t g_spi_a_ok;
extern volatile uint32_t g_spi_a_err;
extern volatile uint32_t g_spi_b_done;

//! DMA buffer accessors for dma_bank_hw.
uint16_t *spi_loop_b_tx_buf(void);
uint16_t *spi_loop_b_rx_buf(void);

#endif // FULLPLATFORM_SPI_LOOP_HW_H
