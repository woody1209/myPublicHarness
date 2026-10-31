//#############################################################################
//
// FILE:   adc_stat.c
//
// TITLE:  Channel statistics accumulator (pure logic).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "adc_stat.h"

#define ADC_STAT_FULL_SCALE  4095U

void adc_stat_reset(adc_stat_t *s)
{
    if (s == 0)
    {
        return;
    }
    s->min = (uint16_t)ADC_STAT_FULL_SCALE;
    s->max = 0U;
    s->sum = 0U;
    s->count = 0U;
}

void adc_stat_push(adc_stat_t *s, uint16_t value)
{
    if (s == 0)
    {
        return;
    }
    if (value < s->min)
    {
        s->min = value;
    }
    if (value > s->max)
    {
        s->max = value;
    }
    s->sum += (uint32_t)value;
    s->count++;
}

bool adc_stat_has_data(const adc_stat_t *s)
{
    return (s != 0) && (s->count > 0U);
}

uint16_t adc_stat_mean(const adc_stat_t *s)
{
    if ((s == 0) || (s->count == 0U))
    {
        return 0U;
    }
    return (uint16_t)(s->sum / s->count);
}
