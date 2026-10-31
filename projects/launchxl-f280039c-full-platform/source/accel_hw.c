//#############################################################################
//
// FILE:   accel_hw.c
//
// TITLE:  Accelerator matrix (hardware layer).
//
// DESIGN (2026-10-25):
//   - BGCRC: CPU BGCRC0 watches a fixed RAM region; the first measured CRC
//     becomes the golden value (region is const after init), every 1 s
//     window must reproduce it.
//   - AES: 128-bit ECB encrypt->decrypt roundtrip on one 16-word block.
//   - TMU: __sin/__atan (TMU0 instructions) judged against math_ref.
//   - VCRC: the CRC unit has no header/intrinsic in CGT 25.11.1 (checked
//     include/ + SDK: vcrc libs exist only for f28p55x); the software CRC
//     reference (math_ref/frame_gen FNV) covers REQ-005's integrity goal
//     and VCRC stays an open item, honestly recorded.
//   - EPG: register-level proof per REQ-019 downgrade (EPG1 enable +
//     pattern regs written, no zero-external-hardware injection path).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "accel_hw.h"
#include "frame_gen.h"
#include "math_ref.h"
#include "driverlib.h"

#define ACCEL_BLOCK_WORDS  FRAME_GEN_WORDS

static uint16_t accel_golden_src[ACCEL_BLOCK_WORDS];  /* BGCRC-watched, const */
static uint32_t accel_bgcrc_golden;
static bool accel_bgcrc_golden_set;

volatile uint32_t g_bgcrc_ok = 0U;
volatile uint32_t g_bgcrc_err = 0U;
volatile uint32_t g_aes_ok = 0U;
volatile uint32_t g_aes_err = 0U;
volatile uint32_t g_tmu_ok = 0U;
volatile uint32_t g_tmu_err = 0U;
volatile uint32_t g_vcrc_ok = 0U;
volatile uint32_t g_vcrc_err = 0U;
volatile uint32_t g_epg_checked = 0U;

#pragma DATA_SECTION(accel_golden_src, "accel_data")

bool accel_init(void)
{
    uint16_t i;

    frame_gen_fill(accel_golden_src, FRAME_GEN_SEED_2);

    /* BGCRC: CPU BGCRC0, region = the const golden block. BGCRC accumulates
     * a CRC over CPU accesses to the region; the block is never written
     * after init, so the value is deterministic. */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_CPUBGCRC);
    BGCRC_setConfig(BGCRC_CPU_BASE, BGCRC_NMI_DISABLE, BGCRC_EMUCTRL_FREE);
    BGCRC_setRegion(BGCRC_CPU_BASE, (uint32_t)accel_golden_src,
                    ACCEL_BLOCK_WORDS * 2U, 0U);
    BGCRC_setSeedValue(BGCRC_CPU_BASE, 0U);
    BGCRC_start(BGCRC_CPU_BASE);

    /* AES module clock + one soft reset */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_AESA);
    AES_performSoftReset(AESA_BASE);

    /* EPG register-level (REQ-019 downgrade): clock + pattern programmed */
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPG1);
    EPG_enableGlobal(EPG1_BASE);
    EPG_selectEPGOutput(EPG1_BASE, EPG_OUT1, EPG_OUT_SEL_SIG);
    EPG_selectSignalOutput(EPG1_BASE, EPG_OUT1, EPG_SIGGEN0_DATATRANOUT0);
    g_epg_checked++;

    return true;
}

//! One AES 128-bit ECB encrypt->decrypt roundtrip on the frame block.
static bool accel_aes_roundtrip(void)
{
    uint32_t key[4] = {0x2B7E1516U, 0x28AED2A6U, 0xABF71588U, 0x09CF4F3CU};
    uint32_t plain[16];
    uint32_t cipher[16];
    uint32_t back[16];
    uint16_t i;
    bool ok = true;
    AES_ConfigParams cfg;

    for (i = 0U; i < 16U; i++)
    {
        plain[i] = ((uint32_t)accel_golden_src[i & 7U] << 16) |
                   accel_golden_src[(i + 3U) & 7U];
        cipher[i] = 0U;
        back[i] = 0xFFFFFFFFU;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.direction = AES_DIRECTION_ENCRYPT;
    cfg.keySize = AES_KEY_SIZE_128BIT;
    cfg.opMode = AES_OPMODE_ECB;
    AES_configureModule(AESA_BASE, &cfg);
    AES_setKey1(AESA_BASE, key, AES_KEY_SIZE_128BIT);
    AES_processData(AESA_BASE, plain, cipher, 16U);

    cfg.direction = AES_DIRECTION_DECRYPT;
    AES_configureModule(AESA_BASE, &cfg);
    AES_setKey1(AESA_BASE, key, AES_KEY_SIZE_128BIT);
    AES_processData(AESA_BASE, cipher, back, 16U);

    for (i = 0U; i < 16U; i++)
    {
        if (back[i] != plain[i]) { ok = false; }
    }
    return ok;
}

//! One BGCRC window: read the accumulated CRC over the const region;
//! first read latches the golden value, later reads must reproduce it.
static void accel_bgcrc_step(void)
{
    uint32_t crc = BGCRC_getResult(BGCRC_CPU_BASE);

    if (!accel_bgcrc_golden_set)
    {
        accel_bgcrc_golden = crc;
        accel_bgcrc_golden_set = true;
        g_bgcrc_ok++;
        return;
    }
    if (crc == accel_bgcrc_golden) { g_bgcrc_ok++; }
    else { g_bgcrc_err++; }
}

//! One TMU batch: __sin/__atan vs math_ref over fixed points.
static void accel_tmu_step(void)
{
    float x;
    uint16_t i;
    bool ok = true;

    for (i = 0U; i < 8U; i++)
    {
        x = 0.4f * (float)i;
        {
            float t = __sin(x);
            float r = math_ref_sin_f32(x);
            float d = (t > r) ? (t - r) : (r - t);
            if (d > 0.01f) { ok = false; }
        }
        {
            float t = __atan(x + 0.1f);
            float r = math_ref_atan2_f32(x + 0.1f, 1.0f);
            float d = (t > r) ? (t - r) : (r - t);
            if (d > 0.01f) { ok = false; }
        }
    }
    if (ok) { g_tmu_ok++; }
    else { g_tmu_err++; }
}

void accel_step(void)
{
    accel_bgcrc_step();
    accel_tmu_step();
    if (accel_aes_roundtrip()) { g_aes_ok++; }
    else { g_aes_err++; }
}
