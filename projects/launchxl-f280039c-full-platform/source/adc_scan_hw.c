//#############################################################################
//
// FILE:   adc_scan_hw.c
//
// TITLE:  ADC A/B/C polled scan with statistics (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "adc_scan_hw.h"
#include "driverlib.h"

//! Sample window in SYSCLK cycles (min 14 for 12-bit per TRM; use 75).
#define ADC_SCAN_ACQPS            75U
//! Power-up settle before the first conversion.
#define ADC_SCAN_POWERUP_LOOPS    ((uint32_t)(120000000U / 3000U))

//! Scan slot map: [mod][soc] = channel; 0xFF = DACA/DACB loopback (discovered).
static const uint16_t adc_scan_channel[ADC_SCAN_MODS][ADC_SCAN_SOCS_PER_MOD] = {
    { ADC_CH_ADCIN3, 0xFFU },   /* ADCA: fixed pin + DACA loopback  */
    { ADC_CH_ADCIN6, ADC_CH_ADCIN7 },  /* ADCB: two fixed pins      */
    { ADC_CH_ADCIN9, 0xFFU }    /* ADCC: fixed pin + DACB loopback  */
};

static const uint32_t adc_scan_base[ADC_SCAN_MODS] = {
    ADCA_BASE, ADCB_BASE, ADCC_BASE
};
static const uint32_t adc_scan_result_base[ADC_SCAN_MODS] = {
    ADCARESULT_BASE, ADCBRESULT_BASE, ADCCRESULT_BASE
};

static uint16_t adc_slot_mod;      /* rotating scan position */
static uint16_t adc_slot_soc;
static bool adc_slot_read_pending;
static uint16_t adc_daca_sample;
static uint16_t adc_dacb_sample;
static bool adc_daca_valid;
static bool adc_dacb_valid;

adc_stat_t g_adc_stats[ADC_SCAN_MODS][ADC_SCAN_SOCS_PER_MOD];

volatile uint16_t g_adc_daca_ch = 0xFFU;
volatile uint16_t g_adc_dacb_ch = 0xFFU;
volatile uint32_t g_adc_conversions = 0U;
volatile uint32_t g_adc_errors = 0U;

static bool adc_scan_init_module(uint32_t base)
{
    ADC_setVREF(base, ADC_REFERENCE_INTERNAL, ADC_REFERENCE_3_3V);
    ADC_setPrescaler(base, ADC_CLK_DIV_4_0);
    ADC_setInterruptPulseMode(base, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(base);
    SysCtl_delay(ADC_SCAN_POWERUP_LOOPS);
    return true;
}

static void adc_scan_setup_soc(uint32_t base, uint32_t soc, uint16_t ch)
{
    ADC_setupSOC(base, (ADC_SOCNumber)soc, ADC_TRIGGER_SW_ONLY,
                 (ADC_Channel)ch, ADC_SCAN_ACQPS);
}

bool adc_scan_init(void)
{
    uint32_t mod;
    uint32_t soc;
    uint16_t ch;

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCA);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCB);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_ADCC);

    for (mod = 0U; mod < ADC_SCAN_MODS; mod++)
    {
        if (!adc_scan_init_module(adc_scan_base[mod]))
        {
            return false;
        }
        for (soc = 0U; soc < ADC_SCAN_SOCS_PER_MOD; soc++)
        {
            ch = adc_scan_channel[mod][soc];
            if (ch != 0xFFU)
            {
                adc_scan_setup_soc(adc_scan_base[mod], soc, ch);
            }
        }
    }

    /* ADCINT1 on ADCA fires at SOC1 EOC - the raw ADCINT1 signal is the
     * DMA CH3 (ADCA1) trigger. The PIE-side enable stays OFF: a PIE flag
     * with no ISR latches group 1 and (with any IER bit ever set) could
     * vector to a default handler; the DMA sees the raw flag regardless. */
    ADC_setInterruptSource(ADCA_BASE, ADC_INT_NUMBER1, ADC_SOC_NUMBER1);
    ADC_setInterruptSOCTrigger(ADCA_BASE, ADC_INT_NUMBER1,
                               ADC_INT_SOC_TRIGGER_NONE);

    adc_slot_mod = 0U;
    adc_slot_soc = 0U;
    adc_slot_read_pending = false;
    adc_daca_valid = false;
    adc_dacb_valid = false;
    {
        uint32_t i;
        uint32_t j;

        for (i = 0U; i < ADC_SCAN_MODS; i++)
        {
            for (j = 0U; j < ADC_SCAN_SOCS_PER_MOD; j++)
            {
                adc_stat_reset(&g_adc_stats[i][j]);
            }
        }
    }
    return true;
}

//! Program the DACA/DACB loopback SOCs once dac_cmpss_hw found the channels.
void adc_scan_bind_dac_channels(uint16_t ch_a, uint16_t ch_b)
{
    if (ch_a != 0xFFU)
    {
        g_adc_daca_ch = ch_a;
        adc_scan_setup_soc(ADCA_BASE, 1U, ch_a);
    }
    if (ch_b != 0xFFU)
    {
        g_adc_dacb_ch = ch_b;
        adc_scan_setup_soc(ADCC_BASE, 1U, ch_b);
    }
}

uint16_t adc_scan_last_mod;
uint16_t adc_scan_last_soc;

void adc_scan_step(void)
{
    uint32_t mod = adc_scan_base[adc_slot_mod];
    uint32_t res = adc_scan_result_base[adc_slot_mod];
    uint32_t soc = adc_slot_soc;
    uint16_t sample;

    if (adc_slot_read_pending)
    {
        /* read the previous conversion, then move the rotation on */
        if (ADC_getInterruptOverflowStatus(mod, ADC_INT_NUMBER1))
        {
            g_adc_errors++;
            ADC_clearInterruptOverflowStatus(mod, ADC_INT_NUMBER1);
        }
        sample = ADC_readResult(res, (ADC_SOCNumber)soc);
        adc_stat_push(&g_adc_stats[adc_slot_mod][adc_slot_soc], sample);
        g_adc_conversions++;

        if ((adc_slot_mod == 0U) && (adc_slot_soc == 1U) &&
            (g_adc_daca_ch != 0xFFU))
        {
            adc_daca_sample = sample;
            adc_daca_valid = true;
        }
        if ((adc_slot_mod == 2U) && (adc_slot_soc == 1U) &&
            (g_adc_dacb_ch != 0xFFU))
        {
            adc_dacb_sample = sample;
            adc_dacb_valid = true;
        }

        adc_scan_last_mod = adc_slot_mod;
        adc_scan_last_soc = adc_slot_soc;

        adc_slot_soc++;
        if (adc_slot_soc >= ADC_SCAN_SOCS_PER_MOD)
        {
            adc_slot_soc = 0U;
            adc_slot_mod++;
            if (adc_slot_mod >= ADC_SCAN_MODS)
            {
                adc_slot_mod = 0U;
            }
        }
        soc = adc_slot_soc;
    }

    ADC_forceSOC(mod, (ADC_SOCNumber)soc);
    adc_slot_read_pending = true;
}

bool adc_scan_get_dac_a_sample(uint16_t *value)
{
    if (!adc_daca_valid || (value == 0)) { return false; }
    *value = adc_daca_sample;
    return true;
}

bool adc_scan_get_dac_b_sample(uint16_t *value)
{
    if (!adc_dacb_valid || (value == 0)) { return false; }
    *value = adc_dacb_sample;
    return true;
}
