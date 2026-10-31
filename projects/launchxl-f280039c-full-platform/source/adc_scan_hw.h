//#############################################################################
//
// FILE:   adc_scan_hw.h
//
// TITLE:  ADC A/B/C polled scan with statistics (hardware layer, REQ-006).
//
// DESCRIPTION:
//   Each ADC module gets two software-triggered SOCs: one fixed analog
//   channel and (on ADCA/ADCC) the DACA/DACB loopback channel. The 1 ms task
//   drives a rotating scan so all six SOCs convert round-robin; results feed
//   the adc_stat accumulators and the DAC loopback verification in
//   dac_cmpss_hw (the raw loopback sample is exported here).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_ADC_SCAN_HW_H
#define FULLPLATFORM_ADC_SCAN_HW_H

#include <stdint.h>
#include <stdbool.h>

#include "adc_stat.h"

//! Number of SOCs per module and number of scan slots.
#define ADC_SCAN_SOCS_PER_MOD      2U
#define ADC_SCAN_MODS              3U

//! Configure all three ADC modules (VREF, prescaler, power, SOC map) and
//! \return false when a module fails its power-up check.
bool adc_scan_init(void);

//! Bind the discovered DACA/DACB loopback channels (0xFF = skip) and
//! reconfigure the corresponding SOCs. Called once by dac_cmpss_hw.
void adc_scan_bind_dac_channels(uint16_t ch_a, uint16_t ch_b);

//! Advance one conversion (1 ms task): read the previous result into the
//! per-slot statistics, then force the next SOC in rotation.
void adc_scan_step(void);

//! Per-slot statistics [mod][soc], reset by the 1 s statistics task.
extern adc_stat_t g_adc_stats[ADC_SCAN_MODS][ADC_SCAN_SOCS_PER_MOD];

//! Latest result of the DACA loopback SOC (ADCA) - consumed by dac_cmpss_hw.
//! \return false until that slot has converted at least once.
bool adc_scan_get_dac_a_sample(uint16_t *value);

//! Latest result of the DACB loopback SOC (ADCC).
bool adc_scan_get_dac_b_sample(uint16_t *value);

//! Discovered ADC channel numbers carrying DACA / DACB outputs (0xFF = unknown).
extern volatile uint16_t g_adc_daca_ch;
extern volatile uint16_t g_adc_dacb_ch;

//! Completed conversion counter and timeout/error counter for the health line.
extern volatile uint32_t g_adc_conversions;
extern volatile uint32_t g_adc_errors;

#endif // FULLPLATFORM_ADC_SCAN_HW_H
