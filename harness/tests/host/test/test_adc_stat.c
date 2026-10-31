//#############################################################################
//
// FILE:   test_adc_stat.c
//
// TITLE:  Host unit tests for the adc_stat pure-logic module (spec 0002).
//
// LICENSE: BSD-3-Clause style.
//
//#############################################################################

#include "fw_test.h"
#include "adc_stat.h"

void adc_stat_tests(void)
{
    adc_stat_t s;

    fw_test_suite_begin("adc_stat");

    fw_test_case_begin("empty accumulator: min/max sentinel, mean 0");
    adc_stat_reset(&s);
    FW_ASSERT_FALSE(adc_stat_has_data(&s));
    FW_ASSERT_EQ_U32(0U, (uint32_t)adc_stat_mean(&s));
    FW_ASSERT_EQ_U32(4095U, (uint32_t)s.min);
    FW_ASSERT_EQ_U32(0U, (uint32_t)s.max);

    fw_test_case_begin("push and mean");
    adc_stat_reset(&s);
    adc_stat_push(&s, 100U);
    adc_stat_push(&s, 200U);
    adc_stat_push(&s, 300U);
    FW_ASSERT_TRUE(adc_stat_has_data(&s));
    FW_ASSERT_EQ_U32(3U, s.count);
    FW_ASSERT_EQ_U32(200U, (uint32_t)adc_stat_mean(&s));   /* 600/3 */
    FW_ASSERT_EQ_U32(100U, (uint32_t)s.min);
    FW_ASSERT_EQ_U32(300U, (uint32_t)s.max);

    fw_test_case_begin("truncated mean and monotone min/max");
    adc_stat_reset(&s);
    adc_stat_push(&s, 1U);
    adc_stat_push(&s, 1U);
    adc_stat_push(&s, 2U);
    FW_ASSERT_EQ_U32(1U, (uint32_t)adc_stat_mean(&s));     /* 4/3 = 1.33 -> 1 */
    adc_stat_push(&s, 4095U);
    FW_ASSERT_EQ_U32(4095U, (uint32_t)s.max);

    fw_test_case_begin("NULL safety and full-scale extremes");
    adc_stat_push(NULL, 5U);
    adc_stat_reset(NULL);
    FW_ASSERT_FALSE(adc_stat_has_data(NULL));
    adc_stat_reset(&s);
    adc_stat_push(&s, 0U);
    adc_stat_push(&s, 4095U);
    FW_ASSERT_EQ_U32(0U, (uint32_t)s.min);
    FW_ASSERT_EQ_U32(4095U, (uint32_t)s.max);
    FW_ASSERT_EQ_U32(2047U, (uint32_t)adc_stat_mean(&s));  /* 4095/2 */

    fw_test_case_begin("count is the mean denominator (large sums stay 32-bit)");
    adc_stat_reset(&s);
    {
        uint16_t i;

        for (i = 0U; i < 4000U; i++)
        {
            adc_stat_push(&s, 4095U);
        }
    }
    FW_ASSERT_EQ_U32(4000U, s.count);
    FW_ASSERT_EQ_U32(4095U, (uint32_t)adc_stat_mean(&s));
}
