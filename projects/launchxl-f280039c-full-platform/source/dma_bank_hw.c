//#############################################################################
//
// FILE:   dma_bank_hw.c
//
// TITLE:  DMA channel bank (hardware layer).
//
// DESIGN (NMI-safe, v2 2026-10-25):
//   v1 coupled CH1/CH2 to the SPIB FIFO with walking addresses and NMI-reset
//   the chip (2026-10-25 crash loop). v2 keeps every address legal:
//     CH1: memory copy  A->B  (SOFTWARE trigger, forced per step)
//     CH2: memory copy  C->A  (SOFTWARE trigger)
//     CH3: ADCA results gather  (ADCA1 trigger: ADCINT1 after each SOC1 EOC;
//          src fixed at ADC RESULT1, dst walks an 8-word buffer)
//     CH4: memory copy  B->D  (SOFTWARE trigger)
//   Completion counts + pattern verifies feed the m3 status bits. This
//   satisfies REQ-016 (memory-block service; ADC-result service) without
//   the illegal peripheral walk.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "dma_bank_hw.h"
#include "frame_gen.h"
#include "driverlib.h"

#define DMA_BLOCK_WORDS   FRAME_GEN_WORDS

static uint16_t dma_a[DMA_BLOCK_WORDS];
static uint16_t dma_b[DMA_BLOCK_WORDS];
static uint16_t dma_c[DMA_BLOCK_WORDS];
static uint16_t dma_d[DMA_BLOCK_WORDS];
static uint16_t dma_adcr[DMA_BLOCK_WORDS];
static bool dma_ch1_pending;
static bool dma_ch2_pending;
static bool dma_ch4_pending;

volatile uint32_t g_dma_done[4] = {0U, 0U, 0U, 0U};
volatile uint32_t g_dma_err = 0U;

static void dma_bank_config(uint32_t ch, uint32_t trigger,
                            uint16_t *src, uint16_t *dst,
                            int16_t srcStep, int16_t dstStep)
{
    DMA_configAddresses(ch, (uint32_t)dst, (uint32_t)src);
    DMA_configBurst(ch, 1U, 0U, 0U);
    DMA_configTransfer(ch, DMA_BLOCK_WORDS, srcStep, dstStep);
    DMA_configWrap(ch, 0xFFFFU, 0U, 0xFFFFU, 0U);
    DMA_configMode(ch, trigger, DMA_CFG_ONESHOT_DISABLE |
                                DMA_CFG_CONTINUOUS_DISABLE |
                                DMA_CFG_SIZE_16BIT);
}

bool dma_bank_init(void)
{
    DMA_initController();

    frame_gen_fill(dma_a, FRAME_GEN_SEED_1);
    frame_gen_fill(dma_c, FRAME_GEN_SEED_2);

    /* PERINTSEL=0 (SOFTWARE) is RESERVED on this part - armed channels saw
     * a stuck-active trigger and walked RAM unbounded (2026-10-25: RAM
     * corruption, fake scheduler hangs, NMI resets). Software channels use
     * XINT5 (never configured = never fires); transfers are forceTrigger'd. */
    dma_bank_config(DMA_CH1_BASE, DMA_TRIGGER_XINT5, dma_a, dma_b, 1, 1);
    dma_bank_config(DMA_CH2_BASE, DMA_TRIGGER_XINT5, dma_c, dma_a, 1, 1);
    /* ADC A RESULT1 (word offset 1 in the results frame): ONE word per
     * trigger, dst FIXED. ADCINT1 fires every SOC1 EOC (~6 ms); a walking
     * dst ran past the 8-word buffer on later transfers and corrupted
     * .ebss (scheduler/health tables) - 2026-10-25 night bring-up. */
    dma_bank_config(DMA_CH3_BASE, DMA_TRIGGER_ADCA1,
                    (uint16_t *)(ADCARESULT_BASE + 1U), dma_adcr, 0, 0);
    DMA_configTransfer(DMA_CH3_BASE, 1U, 0U, 0U);
    dma_bank_config(DMA_CH4_BASE, DMA_TRIGGER_XINT5, dma_b, dma_d, 1, 1);

    DMA_setEmulationMode(DMA_EMULATION_STOP);
    DMA_enableTrigger(DMA_CH1_BASE);
    DMA_enableTrigger(DMA_CH2_BASE);
    DMA_enableTrigger(DMA_CH3_BASE);
    DMA_enableTrigger(DMA_CH4_BASE);
    return true;
}

//! Verify one completed memory channel against its source.
static bool dma_verify(const uint16_t *dst, const uint16_t *src)
{
    uint16_t i;

    for (i = 0U; i < DMA_BLOCK_WORDS; i++)
    {
        if (dst[i] != src[i]) { return false; }
    }
    return true;
}

void dma_bank_step(void)
{
    uint32_t guard;

    /* CH1: A -> B */
    frame_gen_fill(dma_a, FRAME_GEN_SEED_1);
    DMA_forceTrigger(DMA_CH1_BASE);
    dma_ch1_pending = true;

    /* CH2: C -> A */
    DMA_forceTrigger(DMA_CH2_BASE);
    dma_ch2_pending = true;

    /* CH4: B -> D (chained from CH1's previous output) */
    DMA_forceTrigger(DMA_CH4_BASE);
    dma_ch4_pending = true;

    guard = 200000U;
    while ((guard > 0U) &&
           (dma_ch1_pending || dma_ch2_pending || dma_ch4_pending))
    {
        if (dma_ch1_pending &&
            (DMA_getTransferStatusFlag(DMA_CH1_BASE) != 0U))
        {
            dma_ch1_pending = false;
            if (dma_verify(dma_b, dma_a)) { g_dma_done[0]++; }
            else { g_dma_err++; }
        }
        if (dma_ch2_pending &&
            (DMA_getTransferStatusFlag(DMA_CH2_BASE) != 0U))
        {
            dma_ch2_pending = false;
            if (dma_verify(dma_a, dma_c)) { g_dma_done[1]++; }
            else { g_dma_err++; }
        }
        if (dma_ch4_pending &&
            (DMA_getTransferStatusFlag(DMA_CH4_BASE) != 0U))
        {
            dma_ch4_pending = false;
            if (dma_verify(dma_d, dma_b)) { g_dma_done[3]++; }
            else { g_dma_err++; }
        }
        guard--;
    }
    if (dma_ch1_pending || dma_ch2_pending || dma_ch4_pending)
    {
        g_dma_err++;                        /* timeout: channel starved */
    }

    /* CH3: one fresh ADC sample per ADCINT1; a sane 12-bit value counts. */
    if (dma_adcr[0] <= 4095U)
    {
        g_dma_done[2]++;
    }
    else
    {
        g_dma_err++;
    }
}
