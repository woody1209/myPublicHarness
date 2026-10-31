//#############################################################################
//
// FILE:   board_epwm.c
//
// TITLE:  ePWM1/2/3 three-phase complementary PWM with dead-band (hw layer).
//
// DESCRIPTION:
//   Phase A = ePWM1A/B (GPIO0/1), B = ePWM2A/B (GPIO2/3), C = ePWM3A/B
//   (GPIO4/5). All three modules run the SAME in-phase time base
//   (TBPHS = 0), so the duties written by svpwm.c line up across phases.
//
//   Configuration facts (verified against TI C2000Ware_26_01_00_00 driverlib
//   headers and examples/epwm/epwm_ex8_deadband.c, 2026-10-25):
//     - up-down counting: f = SYSCLK / (2 * PRD) = 120 MHz / 12000 = 10 kHz
//     - DBCTL layout: OUT_MODE[1:0]@0, POLSEL[1:0]@2, IN_MODE[1:0]@4
//     - POLSEL = 2 comes from RED=ACTIVE_HIGH + FED=ACTIVE_LOW
//       ("Active High Complementary": OUTB = NOT OUTA)
//     - IN_MODE reset value 0 already routes RED/FED from ePWMA
//     - SVPWM action qualifier (center-aligned):
//         ZRO  -> SET (high)      UP_CMPA  -> CLEAR (low)
//         DOWN_CMPA -> SET (high) duty = CMPA / PRD
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_epwm.h"
#include "board_pins.h"
#include "driverlib.h"
#include "device.h"

//! The three ePWM modules and their pin mux entries (board_pins.h is the
//! only place a raw pin number or mux macro may appear).
static const struct
{
    uint32_t base;
    uint32_t hi_pincfg;
    uint32_t lo_pincfg;
    uint32_t hi_gpio;
    uint32_t lo_gpio;
} board_epwm_phase[3] = {
    { EPWM1_BASE, BOARD_PWM_A_HI_PINCFG, BOARD_PWM_A_LO_PINCFG,
      BOARD_PWM_A_HI_GPIO, BOARD_PWM_A_LO_GPIO },
    { EPWM2_BASE, BOARD_PWM_B_HI_PINCFG, BOARD_PWM_B_LO_PINCFG,
      BOARD_PWM_B_HI_GPIO, BOARD_PWM_B_LO_GPIO },
    { EPWM3_BASE, BOARD_PWM_C_HI_PINCFG, BOARD_PWM_C_LO_PINCFG,
      BOARD_PWM_C_HI_GPIO, BOARD_PWM_C_LO_GPIO }
};

static uint16_t board_epwm_period_to_cmpa(uint16_t permille)
{
    uint32_t cmp = ((uint32_t)permille * (uint32_t)MOTORPWM_PWM_PRD) / 1000U;
    if (cmp > (uint32_t)MOTORPWM_PWM_PRD)
    {
        cmp = (uint32_t)MOTORPWM_PWM_PRD;
    }
    return (uint16_t)cmp;
}

static void board_epwm_config_one(uint32_t base)
{
    //
    // Time base: /1 prescalers, up-down mode, PRD = 6000 (10 kHz), in phase.
    //
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setTimeBasePeriod(base, MOTORPWM_PWM_PRD);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);
    EPWM_setPhaseShift(base, 0U);

    //
    // Start at the zero vector (50% duty) so nothing moves before the
    // first modulate() call lands.
    //
    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A,
                                MOTORPWM_PWM_PRD / 2U);

    //
    // SVPWM action qualifier on output A (dead-band then produces B).
    //
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);

    //
    // Dead-band: RED and FED enabled, full-cycle clock, 120 cycles each.
    // Polarity RED=ACTIVE_HIGH + FED=ACTIVE_LOW -> DBCTL.POLSEL = 2,
    // i.e. OUTB is the complement of OUTA (epwm_ex8 "Active High
    // Complementary").
    //
    EPWM_setDeadBandCounterClock(base, EPWM_DB_COUNTER_CLOCK_FULL_CYCLE);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED,
                                  EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED,
                                  EPWM_DB_POLARITY_ACTIVE_LOW);
    EPWM_setRisingEdgeDelayCount(base, MOTORPWM_PWM_DEADBAND_CYCLES);
    EPWM_setFallingEdgeDelayCount(base, MOTORPWM_PWM_DEADBAND_CYCLES);

    //
    // Trip-zone actions are pre-armed to LOW for both outputs. The event
    // SOURCE (DCBEVT1) is not enabled until board_cmpss_arm_tz(), so no
    // spurious event can act before the CMPSS self-test has passed
    // (REQ-004).
    //
    EPWM_setTripZoneAction(base, EPWM_TZ_ACTION_EVENT_TZA, EPWM_TZ_ACTION_LOW);
    EPWM_setTripZoneAction(base, EPWM_TZ_ACTION_EVENT_TZB, EPWM_TZ_ACTION_LOW);
}

void board_epwm_init(void)
{
    uint32_t i;

    //
    // Clock gates (ADR 0005): ePWM modules are gated off at reset - every
    // register write before this is silently dropped.
    //
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM2);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM3);

    //
    // Synchronise all time bases: configure with TBCLKSYNC disabled, then
    // release so the three modules start counting together (TI epwm
    // examples pattern).
    //
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    for (i = 0U; i < 3U; i++)
    {
        board_epwm_config_one(board_epwm_phase[i].base);
    }
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    //
    // Pin mux + direction (REQ-001): an output pin left as GPxDIR=0 drives
    // nothing, so the direction write is load-bearing, not cosmetic.
    //
    for (i = 0U; i < 3U; i++)
    {
        GPIO_setPinConfig(board_epwm_phase[i].hi_pincfg);
        GPIO_setPinConfig(board_epwm_phase[i].lo_pincfg);
        GPIO_setDirectionMode(board_epwm_phase[i].hi_gpio, GPIO_DIR_MODE_OUT);
        GPIO_setDirectionMode(board_epwm_phase[i].lo_gpio, GPIO_DIR_MODE_OUT);
    }
}

void board_epwm_set_duties(uint16_t a_permille, uint16_t b_permille,
                           uint16_t c_permille)
{
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A,
                                board_epwm_period_to_cmpa(a_permille));
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A,
                                board_epwm_period_to_cmpa(b_permille));
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A,
                                board_epwm_period_to_cmpa(c_permille));
}

bool board_epwm_verify(void)
{
    uint32_t i;
    bool ok = true;

    for (i = 0U; i < 3U; i++)
    {
        uint32_t base = board_epwm_phase[i].base;

        if (EPWM_getTimeBasePeriod(base) != MOTORPWM_PWM_PRD)
        {
            ok = false;
        }
        //
        // F28003x driverlib has no dead-band readback helpers - read the
        // registers directly (hw_epwm.h offsets, word addressing).
        //
        if (HWREGH(base + EPWM_O_DBRED) != MOTORPWM_PWM_DEADBAND_CYCLES)
        {
            ok = false;
        }
        if (HWREGH(base + EPWM_O_DBFED) != MOTORPWM_PWM_DEADBAND_CYCLES)
        {
            ok = false;
        }
        //
        // DBCTL: OUT_MODE[1:0] = 3 (both RED and FED active),
        //        POLSEL[1:0]  = 2 (B is the complement of A).
        //
        if ((HWREGH(base + EPWM_O_DBCTL) & 0x0FU) != 0x0BU)
        {
            ok = false;
        }
    }
    return ok;
}

void board_epwm_report(uint16_t *prd, uint16_t *db, uint16_t *pol)
{
    if (prd != 0)
    {
        *prd = EPWM_getTimeBasePeriod(EPWM1_BASE);
    }
    if (db != 0)
    {
        // No driverlib readback helper on F28003x: read DBRED directly.
        *db = HWREGH(EPWM1_BASE + EPWM_O_DBRED);
    }
    if (pol != 0)
    {
        *pol = (uint16_t)((HWREGH(EPWM1_BASE + EPWM_O_DBCTL) >> 2) & 0x3U);
    }
}
