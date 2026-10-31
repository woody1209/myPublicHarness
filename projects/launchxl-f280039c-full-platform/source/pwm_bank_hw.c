//#############################################################################
//
// FILE:   pwm_bank_hw.c
//
// TITLE:  ePWM bank: all 8 modules / 16 channels (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "pwm_bank_hw.h"
#include "board_pins.h"
#include "driverlib.h"

#define PWM_SYSCLK            120000000U
#define PWM_PRD_10K           (PWM_SYSCLK / 10000U)   /* 12000 */
#define PWM_PRD_20K           (PWM_SYSCLK / 20000U)   /* 6000  */
#define PWM_PRD_50K           (PWM_SYSCLK / 50000U)   /* 2400  */
#define PWM_PRD_1K            (PWM_SYSCLK / 1000U)    /* 120000 */
#define PWM_PRD_5K            (PWM_SYSCLK / 5000U)    /* 24000 */

volatile uint32_t g_pwm_verify_err = 0U;
volatile uint32_t g_pwm_verifies = 0U;

static uint16_t pwm_period_cfg[8];
static uint16_t pwm_cmpa_cfg[8];
static uint16_t pwm_cmpb_cfg[8];

static void pwm_bank_enable_clocks(void)
{
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM3);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM4);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM5);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM6);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM7);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM8);
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

static void pwm_bank_basic(uint32_t base, uint16_t period, uint16_t cmpa)
{
    EPWM_setTimeBasePeriod(base, period);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1,
                           EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP);
    EPWM_setEmulationMode(base, EPWM_EMULATION_FREE_RUN);
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, cmpa);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
}

//! Quadrature pair on one module: A toggles at ZERO/UP_CMPA, B toggles at
//! UP_CMPB/PERIOD -> B lags A by (CMPB/PRD)*360 deg with CMPB = PRD/4.
static void pwm_bank_quad(uint32_t base, uint16_t period, uint16_t cmpa,
                          uint16_t cmpb)
{
    pwm_bank_basic(base, period, cmpa);
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_B, cmpb);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_B,
                                  EPWM_AQ_OUTPUT_TOGGLE,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPB);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_B,
                                  EPWM_AQ_OUTPUT_TOGGLE,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_PERIOD);
}

static bool pwm_bank_pin(uint32_t pin, uint32_t pin_cfg)
{
    GPIO_setPadConfig(pin, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(pin_cfg);
    /* F28003x gates the PERIPHERAL output on GPxDIR too - a peripheral-muxed
     * pin with GPxDIR=0 drives nothing (proven on board 2026-10-25: eCAP saw
     * zero edges until GPADIR was set; TI's own epwm examples only work
     * because their syscfg Board_init sets the direction). */
    GPIO_setDirectionMode(pin, GPIO_DIR_MODE_OUT);
    return true;
}

bool pwm_bank_init(void)
{
    uint16_t mep_read;
    bool ok = true;

    pwm_bank_enable_clocks();

    // EPWM1 10 kHz 50 % on GPIO0 (eCAP1 loopback; TZ target from CMPSS1).
    pwm_bank_basic(EPWM1_BASE, PWM_PRD_10K, PWM_PRD_10K / 2U);
    pwm_bank_pin(BOARD_EPWM1A_GPIO, BOARD_EPWM1A_PINCFG);

    // EPWM2 20 kHz 50 % on GPIO2 (eCAP2 loopback).
    pwm_bank_basic(EPWM2_BASE, PWM_PRD_20K, PWM_PRD_20K / 2U);
    pwm_bank_pin(BOARD_EPWM2A_GPIO, BOARD_EPWM2A_PINCFG);

    // EPWM3 50 kHz + HRPWM MEP on channel A (eCAP3/HRCAP loopback).
    pwm_bank_basic(EPWM3_BASE, PWM_PRD_50K, PWM_PRD_50K / 2U);
    pwm_bank_pin(BOARD_EPWM3A_GPIO, BOARD_EPWM3A_PINCFG);
    HRPWM_setMEPControlMode(EPWM3_BASE, HRPWM_CHANNEL_A,
                            HRPWM_MEP_CTRL_RISING_EDGE);
    HRPWM_setMEPStep(EPWM3_BASE, 5U);
    HRPWM_enableAutoConversion(EPWM3_BASE);

    // EPWM4 1 kHz quadrature pair on GPIO6/7 -> eQEP1.
    pwm_bank_quad(EPWM4_BASE, PWM_PRD_1K, PWM_PRD_1K / 2U, PWM_PRD_1K / 4U);
    pwm_bank_pin(BOARD_EPWM4A_GPIO, BOARD_EPWM4A_PINCFG);
    pwm_bank_pin(BOARD_EPWM4B_GPIO, BOARD_EPWM4B_PINCFG);

    // EPWM5/6/7 10 kHz 25/50/75 % - no pins (register-level evidence).
    pwm_bank_basic(EPWM5_BASE, PWM_PRD_10K, PWM_PRD_10K / 4U);
    pwm_bank_basic(EPWM6_BASE, PWM_PRD_10K, PWM_PRD_10K / 2U);
    pwm_bank_basic(EPWM7_BASE, PWM_PRD_10K, (PWM_PRD_10K / 4U) * 3U);

    // EPWM8 5 kHz quadrature pair on GPIO14/15 -> eQEP2.
    pwm_bank_quad(EPWM8_BASE, PWM_PRD_5K, PWM_PRD_5K / 2U, PWM_PRD_5K / 4U);
    pwm_bank_pin(BOARD_EPWM8A_GPIO, BOARD_EPWM8A_PINCFG);
    pwm_bank_pin(BOARD_EPWM8B_GPIO, BOARD_EPWM8B_PINCFG);

    // TBCLKSYNC last: all 8 modules start in lockstep.
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // Register-level readback.
    pwm_period_cfg[0] = EPWM_getTimeBasePeriod(EPWM1_BASE);
    pwm_cmpa_cfg[0] = EPWM_getCounterCompareValue(EPWM1_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[1] = EPWM_getTimeBasePeriod(EPWM2_BASE);
    pwm_cmpa_cfg[1] = EPWM_getCounterCompareValue(EPWM2_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[2] = EPWM_getTimeBasePeriod(EPWM3_BASE);
    pwm_cmpa_cfg[2] = EPWM_getCounterCompareValue(EPWM3_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[3] = EPWM_getTimeBasePeriod(EPWM4_BASE);
    pwm_cmpa_cfg[3] = EPWM_getCounterCompareValue(EPWM4_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_cmpb_cfg[3] = EPWM_getCounterCompareValue(EPWM4_BASE,
                                                  EPWM_COUNTER_COMPARE_B);
    pwm_period_cfg[4] = EPWM_getTimeBasePeriod(EPWM5_BASE);
    pwm_cmpa_cfg[4] = EPWM_getCounterCompareValue(EPWM5_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[5] = EPWM_getTimeBasePeriod(EPWM6_BASE);
    pwm_cmpa_cfg[5] = EPWM_getCounterCompareValue(EPWM6_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[6] = EPWM_getTimeBasePeriod(EPWM7_BASE);
    pwm_cmpa_cfg[6] = EPWM_getCounterCompareValue(EPWM7_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_period_cfg[7] = EPWM_getTimeBasePeriod(EPWM8_BASE);
    pwm_cmpa_cfg[7] = EPWM_getCounterCompareValue(EPWM8_BASE,
                                                  EPWM_COUNTER_COMPARE_A);
    pwm_cmpb_cfg[7] = EPWM_getCounterCompareValue(EPWM8_BASE,
                                                  EPWM_COUNTER_COMPARE_B);

    // HRPWM MEP: HRMSTEP is the HARDWARE-CALIBRATED MEP step size (the core
    // measures its own MEP resolution); HRPWM_setMEPStep(5) seeds it but the
    // readback returns the measured value, not the written one - so the
    // correct check is "register responds in range", not "== 5" (the ==5
    // check misfired on the first board run, 2026-10-25).
    mep_read = HWREGH(EPWM3_BASE + HRPWM_O_HRMSTEP) & HRPWM_HRMSTEP_HRMSTEP_M;
    if (mep_read > 100U)
    {
        ok = false;                        /* implausible MEP calibration */
    }

    return ok;
}

void pwm_bank_verify(void)
{
    uint32_t err = 0U;

    g_pwm_verifies++;
    if (EPWM_getTimeBasePeriod(EPWM1_BASE) != pwm_period_cfg[0]) { err++; }
    if (EPWM_getCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A) !=
        pwm_cmpa_cfg[0]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM2_BASE) != pwm_period_cfg[1]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM3_BASE) != pwm_period_cfg[2]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM4_BASE) != pwm_period_cfg[3]) { err++; }
    if (EPWM_getCounterCompareValue(EPWM4_BASE, EPWM_COUNTER_COMPARE_B) !=
        pwm_cmpb_cfg[3]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM5_BASE) != pwm_period_cfg[4]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM6_BASE) != pwm_period_cfg[5]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM7_BASE) != pwm_period_cfg[6]) { err++; }
    if (EPWM_getTimeBasePeriod(EPWM8_BASE) != pwm_period_cfg[7]) { err++; }
    if (EPWM_getCounterCompareValue(EPWM8_BASE, EPWM_COUNTER_COMPARE_B) !=
        pwm_cmpb_cfg[7]) { err++; }
    g_pwm_verify_err += err;
}
