//#############################################################################
//
// FILE:   board_pins.h
//
// TITLE:  Board pin allocation - THE single source of truth for this project.
//
// DESCRIPTION:
//   Every GPIO number in this firmware must come from here. Business code
//   references symbolic names only (AGENTS.md §4).
//   Matrix provenance: specs/0002-full-platform/design.md §6 (pin_map.h
//   candidates verified 2026-10-24). Each GPIO appears in exactly ONE role.
//
//   Reserved, never reassigned:
//     - GPIO35-38: JTAG (TMS/TDO/TCK/TDI) - muxing these kills the debug port.
//     - GPIO20/22: user LEDs LED4/LED5 (active low, SN74LVC2G07).
//     - GPIO28/29: SCIA console to the XDS110 virtual COM.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef FULLPLATFORM_BOARD_PINS_H
#define FULLPLATFORM_BOARD_PINS_H

// ---------------------------------------------------------------------------
// LEDs (same wiring as the reference project)
// ---------------------------------------------------------------------------
#define LEDCHASER_CHANNEL_COUNT      2U
#define BOARD_LED4_GPIO              20U
#define BOARD_LED5_GPIO              22U
#define BOARD_LED4_PINCFG            GPIO_20_GPIO20
#define BOARD_LED5_PINCFG            GPIO_22_GPIO22
#define BOARD_LED_ACTIVE_LOW         true

// ---------------------------------------------------------------------------
// SCIA console (XDS110 virtual COM, 115200-8N1)
// ---------------------------------------------------------------------------
#define BOARD_SCI_BASE               SCIA_BASE
#define BOARD_SCI_TX_GPIO            29U
#define BOARD_SCI_RX_GPIO            28U
#define BOARD_SCI_TX_PINCFG          GPIO_29_SCIA_TX
#define BOARD_SCI_RX_PINCFG          GPIO_28_SCIA_RX
#define BOARD_SCI_BAUD               115200U
#define BOARD_SCI_LSPCLK_HZ          30000000U

// ---------------------------------------------------------------------------
// ePWM outputs driving pad-loopback captures (design.md §6)
// ---------------------------------------------------------------------------
#define BOARD_EPWM1A_GPIO            0U      /* -> eCAP1 via INPUT7  */
#define BOARD_EPWM2A_GPIO            2U      /* -> eCAP2 via INPUT8  */
#define BOARD_EPWM3A_GPIO            4U      /* -> eCAP3(HRCAP) via INPUT9, HRPWM */
#define BOARD_EPWM4A_GPIO            6U      /* quadrature pair 1 -> eQEP1 */
#define BOARD_EPWM4B_GPIO            7U
#define BOARD_EPWM8A_GPIO            14U     /* quadrature pair 2 -> eQEP2 */
#define BOARD_EPWM8B_GPIO            15U

#define BOARD_EPWM1A_PINCFG          GPIO_0_EPWM1_A
#define BOARD_EPWM2A_PINCFG          GPIO_2_EPWM2_A
#define BOARD_EPWM3A_PINCFG          GPIO_4_EPWM3_A
#define BOARD_EPWM4A_PINCFG          GPIO_6_EPWM4_A
#define BOARD_EPWM4B_PINCFG          GPIO_7_EPWM4_B
#define BOARD_EPWM8A_PINCFG          GPIO_14_EPWM8_A
#define BOARD_EPWM8B_PINCFG          GPIO_15_EPWM8_B

//! Input X-BAR inputs fed by the pad-loopback GPIOs above.
#define BOARD_INPUT_ECAP1            XBAR_INPUT7   /* GPIO0  */
#define BOARD_INPUT_ECAP2            XBAR_INPUT8   /* GPIO2  */
#define BOARD_INPUT_ECAP3            XBAR_INPUT9   /* GPIO4  */

//! eQEP inputs come from the EPWM X-BAR trips. Empirical mapping (diag21,
//! 2026-10-25): the eQEP source mux numbers the X-BAR outputs as
//! PWMXBARn = TRIP(n+3) - PWMXBAR3 = TRIP6 does NOT exist on this part, so
//! the valid sources for our 4 signals are PWMXBAR2/4/5/6 = TRIP5/7/8/9.
//! (The first attempt used PWMXBAR1..4 = a static CMPSS signal for QEP1B and
//! mismatched frequencies for QEP2 - EQEP1 counted 0, EQEP2 counted noise.)
#define BOARD_INPUT_QEP1A            XBAR_INPUT1   /* GPIO6  = EPWM4A */
#define BOARD_INPUT_QEP1B            XBAR_INPUT2   /* GPIO7  = EPWM4B */
#define BOARD_INPUT_QEP2A            XBAR_INPUT3   /* GPIO14 = EPWM8A */
#define BOARD_INPUT_QEP2B            XBAR_INPUT4   /* GPIO15 = EPWM8B */

//! EPWM X-BAR trip assignment (available outputs on F28003x: 4,5,7,8..12).
#define BOARD_TRIP_CMPSS1            XBAR_TRIP4    /* mux00 = CMPSS1_CTRIPH   */
#define BOARD_TRIP_QEP1A             XBAR_TRIP5    /* mux01 = INPUTXBAR1      */
#define BOARD_TRIP_QEP1B             XBAR_TRIP7    /* mux03 = INPUTXBAR2      */
#define BOARD_TRIP_QEP2A             XBAR_TRIP8    /* mux05 = INPUTXBAR3      */
#define BOARD_TRIP_QEP2B             XBAR_TRIP9    /* mux07 = INPUTXBAR4      */

#define BOARD_QEP1A_SOURCE           EQEP_SOURCE_PWMXBAR2
#define BOARD_QEP1B_SOURCE           EQEP_SOURCE_PWMXBAR4
#define BOARD_QEP2A_SOURCE           EQEP_SOURCE_PWMXBAR5
#define BOARD_QEP2B_SOURCE           EQEP_SOURCE_PWMXBAR6

// ---------------------------------------------------------------------------
// SPI A/B (digital loopback; pins registered for completeness/debugging)
// ---------------------------------------------------------------------------
#define BOARD_SPIA_GPIO_SIMO         16U
#define BOARD_SPIA_GPIO_SOMI         17U
#define BOARD_SPIA_GPIO_CLK          18U
#define BOARD_SPIA_PINCFG_SIMO       GPIO_16_SPIA_SIMO
#define BOARD_SPIA_PINCFG_SOMI       GPIO_17_SPIA_SOMI
#define BOARD_SPIA_PINCFG_CLK        GPIO_18_SPIA_CLK

#define BOARD_SPIB_GPIO_SIMO         24U
#define BOARD_SPIB_GPIO_SOMI         25U
#define BOARD_SPIB_GPIO_CLK          26U
#define BOARD_SPIB_PINCFG_SIMO       GPIO_24_SPIB_SIMO
#define BOARD_SPIB_PINCFG_SOMI       GPIO_25_SPIB_SOMI
#define BOARD_SPIB_PINCFG_CLK        GPIO_26_SPIB_CLK

// ---------------------------------------------------------------------------
// I2C A/B (controller loopback is the primary evidence; A<->B jumper wires
// GPIO10<->GPIO34 and GPIO1<->GPIO33 are the optional physical-layer proof)
// ---------------------------------------------------------------------------
#define BOARD_I2CA_GPIO_SDA          10U
#define BOARD_I2CA_GPIO_SCL          1U
#define BOARD_I2CA_PINCFG_SDA        GPIO_10_I2CA_SDA
#define BOARD_I2CA_PINCFG_SCL        GPIO_1_I2CA_SCL

#define BOARD_I2CB_GPIO_SDA          34U
#define BOARD_I2CB_GPIO_SCL          33U
#define BOARD_I2CB_PINCFG_SDA        GPIO_34_I2CB_SDA
#define BOARD_I2CB_PINCFG_SCL        GPIO_33_I2CB_SCL

// ---------------------------------------------------------------------------
// SCIB (internal loopback; pins registered only)
// ---------------------------------------------------------------------------
#define BOARD_SCIB_GPIO_TX           12U
#define BOARD_SCIB_GPIO_RX           13U
#define BOARD_SCIB_PINCFG_TX         GPIO_12_SCIB_TX
#define BOARD_SCIB_PINCFG_RX         GPIO_13_SCIB_RX

// ---------------------------------------------------------------------------
// LIN A/B (digital loopback; pins registered only)
// ---------------------------------------------------------------------------
#define BOARD_LINA_GPIO_TX           32U
#define BOARD_LINA_GPIO_RX           23U
#define BOARD_LINA_PINCFG_TX         GPIO_32_LINA_TX
#define BOARD_LINA_PINCFG_RX         GPIO_23_LINA_RX

#define BOARD_LINB_GPIO_TX           44U
#define BOARD_LINB_GPIO_RX           41U
#define BOARD_LINB_PINCFG_TX         GPIO_44_LINB_TX
#define BOARD_LINB_PINCFG_RX         GPIO_41_LINB_RX

// ---------------------------------------------------------------------------
// MCAN A (self-test loopback; pins registered only)
// ---------------------------------------------------------------------------
#define BOARD_MCAN_GPIO_TX           48U
#define BOARD_MCAN_GPIO_RX           49U
#define BOARD_MCAN_PINCFG_TX         GPIO_48_CANA_TX
#define BOARD_MCAN_PINCFG_RX         GPIO_49_CANA_RX

// ---------------------------------------------------------------------------
// FSI TX/RX (RX internal loopback is the primary evidence; optional jumper
// wires GPIO45<->GPIO3 (D0) and GPIO51<->GPIO39 (CLK))
// ---------------------------------------------------------------------------
#define BOARD_FSITX_GPIO_D0          45U
#define BOARD_FSITX_GPIO_CLK         51U
#define BOARD_FSITX_PINCFG_D0        GPIO_45_FSITXA_D0
#define BOARD_FSITX_PINCFG_CLK       GPIO_51_FSITXA_CLK

#define BOARD_FSIRX_GPIO_D0          3U
#define BOARD_FSIRX_GPIO_CLK         39U
#define BOARD_FSIRX_PINCFG_D0        GPIO_3_FSIRXA_D0
#define BOARD_FSIRX_PINCFG_CLK       GPIO_39_FSIRXA_CLK

#endif // FULLPLATFORM_BOARD_PINS_H
