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
//   Facts (TI SPRUJ31 LAUNCHXL-F280039C User's Guide §3.1.3):
//     - LED4 (red)   = GPIO20, LED5 (green) = GPIO22
//     - Both are driven through SN74LVC2G07DBVR in an ACTIVE-LOW configuration:
//       drive the GPIO low to turn the LED on.
//     - XDS110 virtual COM: SCIA TX = GPIO29, RX = GPIO28
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef LEDCHASER_BOARD_PINS_H
#define LEDCHASER_BOARD_PINS_H

//! Number of chaser channels instantiated on this board (LED4 + LED5).
#define LEDCHASER_CHANNEL_COUNT      2U

//! LED channels: index 0 = LED4 (red), index 1 = LED5 (green).
#define BOARD_LED4_GPIO              20U
#define BOARD_LED5_GPIO              22U
#define BOARD_LED4_PINCFG            GPIO_20_GPIO20
#define BOARD_LED5_PINCFG            GPIO_22_GPIO22

//! LaunchPad LEDs are wired active-low (drive low = on).
#define BOARD_LED_ACTIVE_LOW         true

//! SCI (XDS110 virtual COM) wiring.
#define BOARD_SCI_BASE               SCIA_BASE
#define BOARD_SCI_TX_GPIO            29U
#define BOARD_SCI_RX_GPIO            28U
#define BOARD_SCI_TX_PINCFG          GPIO_29_SCIA_TX
#define BOARD_SCI_RX_PINCFG          GPIO_28_SCIA_RX
#define BOARD_SCI_BAUD               115200U

//! LSPCLK is set explicitly by board_clock.c (SYSCLK / 4 = 30 MHz).
#define BOARD_SCI_LSPCLK_HZ          30000000U

#endif // LEDCHASER_BOARD_PINS_H
