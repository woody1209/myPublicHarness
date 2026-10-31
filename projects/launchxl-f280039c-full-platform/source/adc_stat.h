//#############################################################################
//
// FILE:   adc_stat.h
//
// TITLE:  Channel statistics accumulator (pure logic, hardware independent).
//
// DESCRIPTION:
//   min/max/mean statistics for one ADC channel over a sampling window.
//   Only C standard headers - host unit tests run the exact target code.
//   Mean is integer-truncated on purpose: the judgement is rail/consistency
//   banding, not precision metrology.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_ADC_STAT_H
#define FULLPLATFORM_ADC_STAT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint16_t min;
    uint16_t max;
    uint32_t sum;
    uint32_t count;
} adc_stat_t;

//! Reset the accumulator to an empty state (min = 4095, max = 0).
void adc_stat_reset(adc_stat_t *s);

//! Push one 12-bit sample. No-op when s is NULL.
void adc_stat_push(adc_stat_t *s, uint16_t value);

//! \return true when at least one sample was pushed.
bool adc_stat_has_data(const adc_stat_t *s);

//! \return truncated mean (sum / count); 0 when empty.
uint16_t adc_stat_mean(const adc_stat_t *s);

#endif // FULLPLATFORM_ADC_STAT_H
