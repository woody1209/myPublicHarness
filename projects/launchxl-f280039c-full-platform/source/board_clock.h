//#############################################################################
//
// FILE:   board_clock.h
//
// TITLE:  System clock and timing services (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#ifndef LEDCHASER_BOARD_CLOCK_H
#define LEDCHASER_BOARD_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

//! PLL source: internal oscillator INTOSC2 = 10 MHz (no external crystal needed).
#define BOARD_OSC_SRC_FREQ_HZ        10000000U

//! Target SYSCLK after PLL: 10 MHz * IMULT(48) / (REFDIV(1) * ODIV(4) * SYSDIV(1)).
#define BOARD_SYSCLK_FREQ_HZ         120000000U

//! LSPCLK = SYSCLK / 4.
#define BOARD_LSPCLK_FREQ_HZ         30000000U

//
// ---------------------------------------------------------------------------
// .TI.ramfunc relocation  (REQUIRED before any SysCtl_* call)
// ---------------------------------------------------------------------------
//
// C2000 driverlib emits SysCtl_delay() as inline assembly into the section
// ".TI.ramfunc" (see sysctl.c, SYSCTL_DELAY) because it is called while the
// clock tree / flash wait states are being reconfigured and must therefore
// execute from RAM. The flash linker command splits that section:
//
//     .TI.ramfunc : LOAD = FLASH_BANK0_SEC1, RUN = RAMLS0 (= 0x8000),
//                   LOAD_START(RamfuncsLoadStart), LOAD_SIZE(RamfuncsLoadSize),
//                   RUN_START(RamfuncsRunStart), ...
//
// The C run-time (_c_int00) clears .bss and expands .cinit, but it does NOT
// perform a load-to-run copy of a section that has separate LOAD and RUN
// addresses. Without an explicit memcpy the CPU branches into empty RAMLS0,
// executes 0x0000 as an instruction and takes an illegal-instruction trap
// (ITRAP). Because the PIE is disabled until Interrupt_initModule() runs, the
// trap vector is fetched from the boot ROM, whose CPU1BROM_itrapISR enables
// the watchdog and waits for a reset - the firmware therefore "dies a few
// milliseconds after start" with no console output and no LED activity.
//
// Rule: board_ramfunc_relocate() MUST be called before the first SysCtl_*
// call, in particular before board_clock_init(). board_clock_init() also does
// it defensively so that no caller can get this wrong.
//
void board_ramfunc_relocate(void);

//! \return true when .TI.ramfunc at its RUN address matches the flash image
bool board_ramfunc_ready(void);

//! \return size in bytes of the relocated .TI.ramfunc section (0 if nothing
//!         needs relocating, e.g. the RAM build configuration)
uint32_t board_ramfunc_size(void);

//! Initialise clocks: disable watchdog, configure the PLL, set LSPCLK.
//! \return true when SYSCLK verifies as BOARD_SYSCLK_FREQ_HZ, false otherwise
bool board_clock_init(void);

//! Start the CPU timer used as the chaser tick source.
//! \param tick_hz tick rate in Hz (e.g. 1000 for a 1 ms base period)
void board_timer_init(uint32_t tick_hz);

//! \return true once the period elapsed since the previous acknowledgement
bool board_timer_tick_elapsed(void);

//! Acknowledge (clear) the pending tick.
void board_timer_tick_ack(void);

//! Blocking delay in milliseconds, based on SysCtl_delay() and the verified
//! SYSCLK frequency. Used by the fail-safe path and startup sequencing.
void board_delay_ms(uint32_t ms);

#endif // LEDCHASER_BOARD_CLOCK_H
