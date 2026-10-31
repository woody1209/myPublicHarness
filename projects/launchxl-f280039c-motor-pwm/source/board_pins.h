//#############################################################################
//
// FILE:   board_pins.h
//
// TITLE:  Board pin allocation - THE single source of truth for this project.
//
// DESCRIPTION:
//   Every GPIO number in this firmware must come from here. Business code
//   references symbolic names only (AGENTS.md §4).
//
//   Facts (TI SPRUJ31 LAUNCHXL-F280039C User's Guide §3.1.3, TMS320F280039C
//   data sheet):
//     - ePWM1A/B = GPIO0/1, ePWM2A/B = GPIO2/3, ePWM3A/B = GPIO4/5
//       (motor-pwm phase A/B/C, complementary pairs)
//     - A2 (pin 17) is a dedicated analog pin shared by ADCA channel 2 and
//       CMPSS1 positive input (CMPIN1P); it has NO GPIO mux function
//     - LED4 (red) = GPIO20, LED5 (green) = GPIO22, active-low
//     - XDS110 virtual COM: SCIA TX = GPIO29, RX = GPIO28
//     - GPIO35-38 are the cJTAG debug pins: never bound as GPIO
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef MOTORPWM_BOARD_PINS_H
#define MOTORPWM_BOARD_PINS_H

//! PWM carrier period: 120 MHz SYSCLK, up-down mode -> 10 kHz.
#define MOTORPWM_PWM_PRD                6000U
//! Dead-band RED/FED delay in TBCLK cycles (1 us @ 120 MHz).
#define MOTORPWM_PWM_DEADBAND_CYCLES    120U

//! Phase A: ePWM1A (high side) / ePWM1B (low side).
#define BOARD_PWM_A_HI_PINCFG           GPIO_0_EPWM1_A
#define BOARD_PWM_A_LO_PINCFG           GPIO_1_EPWM1_B
#define BOARD_PWM_A_HI_GPIO             0U
#define BOARD_PWM_A_LO_GPIO             1U

//! Phase B: ePWM2A / ePWM2B.
#define BOARD_PWM_B_HI_PINCFG           GPIO_2_EPWM2_A
#define BOARD_PWM_B_LO_PINCFG           GPIO_3_EPWM2_B
#define BOARD_PWM_B_HI_GPIO             2U
#define BOARD_PWM_B_LO_GPIO             3U

//! Phase C: ePWM3A / ePWM3B.
#define BOARD_PWM_C_HI_PINCFG           GPIO_4_EPWM3_A
#define BOARD_PWM_C_LO_PINCFG           GPIO_5_EPWM3_B
#define BOARD_PWM_C_HI_GPIO             4U
#define BOARD_PWM_C_LO_GPIO             5U

//! ISENSE analog pin A2: ADCA channel 2 AND CMPSS1 positive input (same pin).
#define BOARD_ISENSE_ADC_CHANNEL        ADC_CH_ADCIN2
#define BOARD_ISENSE_ADC_SOC            0U

//! CMPSS1 trip route: ePWM X-BAR TRIP4, mux00 = CMPSS1_CTRIPH.
#define BOARD_TRIP_CMPSS1               XBAR_TRIP4

//! LED channels: index 0 = LED4 (red), index 1 = LED5 (green).
#define BOARD_LED4_GPIO                 20U
#define BOARD_LED5_GPIO                 22U
#define BOARD_LED4_PINCFG               GPIO_20_GPIO20
#define BOARD_LED5_PINCFG               GPIO_22_GPIO22

//! LaunchPad LEDs are wired active-low (drive low = on).
#define BOARD_LED_ACTIVE_LOW            true

//! SCI (XDS110 virtual COM) wiring.
#define BOARD_SCI_BASE                  SCIA_BASE
#define BOARD_SCI_TX_GPIO               29U
#define BOARD_SCI_RX_GPIO               28U
#define BOARD_SCI_TX_PINCFG             GPIO_29_SCIA_TX
#define BOARD_SCI_RX_PINCFG             GPIO_28_SCIA_RX
#define BOARD_SCI_BAUD                  115200U

//! LSPCLK is set explicitly by board_clock.c (SYSCLK / 4 = 30 MHz).
#define BOARD_SCI_LSPCLK_HZ             30000000U

#endif // MOTORPWM_BOARD_PINS_H
