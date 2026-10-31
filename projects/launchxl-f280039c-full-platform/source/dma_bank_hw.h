//#############################################################################
//
// FILE:   dma_bank_hw.h
//
// TITLE:  DMA channel bank (hardware layer, REQ-016).
//
// DESCRIPTION:
//   Four DMA channels, each with a countable completion path:
//     CH1: SPIB TX  (trigger SPIBTX)  - feeds the SPIB loopback block
//     CH2: SPIB RX  (trigger SPIBRX)  - collects the loopback block
//     CH3: memory block copy A->B (SOFTWARE trigger, forced per step)
//     CH4: memory block copy B->A (SOFTWARE trigger, forced per step)
//   Completed transfers are verified (frame_gen pattern) and counted; a
//   channel whose completion count stalls unhooks its m3 status bit.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_DMA_BANK_HW_H
#define FULLPLATFORM_DMA_BANK_HW_H

#include <stdint.h>
#include <stdbool.h>

bool dma_bank_init(void);

//! Kick the software channels + SPIB transaction (1 s task step), then
//! verify whatever completed.
void dma_bank_step(void);

extern volatile uint32_t g_dma_done[4];
extern volatile uint32_t g_dma_err;

#endif // FULLPLATFORM_DMA_BANK_HW_H
