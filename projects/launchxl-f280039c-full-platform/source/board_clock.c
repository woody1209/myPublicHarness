//#############################################################################
//
// FILE:   board_clock.c
//
// TITLE:  System clock and timing services (hardware layer).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_clock.h"
#include "driverlib.h"
#include "device.h"

#include <string.h>

#if defined(_FLASH)
//
// Linker-generated symbols from 28003x_led_chaser_flash_lnk.cmd
// (.TI.ramfunc LOAD/RUN split). For a linker symbol, "&symbol" evaluates to
// the symbol's value, which is how the TI reference code reads them:
//     memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
//
extern uint16_t RamfuncsRunStart, RamfuncsLoadStart, RamfuncsLoadSize;
#endif

//
// Relocate .TI.ramfunc from its flash LOAD address to its RAM RUN address.
// See board_clock.h for why this is mandatory.
//
void board_ramfunc_relocate(void)
{
#if defined(_FLASH)
    memcpy(&RamfuncsRunStart, &RamfuncsLoadStart, (size_t)&RamfuncsLoadSize);
#endif
}

//
// Verify the relocation actually happened (guards against a linker-command
// change that drops the Ramfuncs* symbols, or a stale image).
//
bool board_ramfunc_ready(void)
{
#if defined(_FLASH)
    const uint16_t *run  = (const uint16_t *)&RamfuncsRunStart;
    const uint16_t *load = (const uint16_t *)&RamfuncsLoadStart;
    uint32_t words = board_ramfunc_size() / sizeof(uint16_t);
    uint32_t i;

    for (i = 0U; i < words; i++)
    {
        if (run[i] != load[i])
        {
            return false;
        }
    }
#endif
    return true;
}

uint32_t board_ramfunc_size(void)
{
#if defined(_FLASH)
    return (uint32_t)(uintptr_t)&RamfuncsLoadSize;
#else
    return 0U;
#endif
}

bool board_clock_init(void)
{
    //
    // Defensive: SysCtl_setClock() calls SysCtl_delay(), which lives in
    // .TI.ramfunc. Relocate first so the call cannot branch into empty RAM.
    //
    board_ramfunc_relocate();

    //
    // The watchdog is enabled out of reset: disable it before doing anything
    // else, otherwise a slow start-up sequence trips a reset.
    //
    SysCtl_disableWatchdog();

    //
    // INTOSC2 (10 MHz) -> PLL -> 120 MHz SYSCLK.
    // PLLSYSCLK = 10 MHz * IMULT(48) / (REFDIV(1) * ODIV(4) * SYSDIV(1)).
    //
    SysCtl_setClock(SYSCTL_OSCSRC_OSC2 | SYSCTL_IMULT(48) |
                    SYSCTL_REFDIV(1) | SYSCTL_ODIV(4) |
                    SYSCTL_SYSDIV(1) | SYSCTL_PLL_ENABLE |
                    SYSCTL_DCC_BASE_0);

    //
    // LSPCLK = SYSCLK / 4 = 30 MHz. Set explicitly so the SCI baud rate
    // calculation is deterministic.
    //
    SysCtl_setLowSpeedClock(SYSCTL_LSPCLK_PRESCALE_4);

    //
    // SCIA peripheral clock gate (CPUSYS_PCLKCR7 bit0). Gated off at reset:
    // without this every SCI register write is silently dropped (CTL1 stays
    // 0x0000, no TX output ever) - on-target evidence 2026-10-17 diag18.
    // TI's f28003x InitSysCtrl() sets the same bit.
    //
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SCIA);

    //
    // Verify: a mismatch means the firmware would run at an unexpected speed
    // (and every timing assumption would be wrong), so surface it.
    //
    return (SysCtl_getClock(BOARD_OSC_SRC_FREQ_HZ) == BOARD_SYSCLK_FREQ_HZ);
}

void board_timer_init(uint32_t tick_hz)
{
    uint32_t period;

    if (tick_hz == 0U)
    {
        return;
    }

    //
    // CPUTIMER0 decrements at SYSCLK with TPR=0 (classic C28x convention).
    // Measured on-target 2026-10-17 (halt-free probe, diag19): PRD=30000 gave
    // 51.4 ms for a 200-overflow step = 257 us per overflow, against the
    // 250 us that PRD/SYSCLK predicts. So the timer really runs at SYSCLK, and
    // the old SYSCLK/4 period made every software "ms" tick 4x too short (the
    // chaser ran ~50 ms per step instead of 200 ms). The apparent 400 ms
    // cadence seen before that was not a clock prescale - it was the
    // halt-freeze distortion cured by CPUTimer_setEmulationMode() below.
    //
    period = BOARD_SYSCLK_FREQ_HZ / tick_hz;

    CPUTimer_setPeriod(CPUTIMER0_BASE, period);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    //
    // Keep the timer counting while the core is halted in the debugger, so a
    // halt does not freeze the counter mid-period. Note this cannot make the
    // step rate halt-proof: TIF is a single flag, so every overflow produced
    // during a halt collapses into one increment when the polled main loop
    // resumes (HIL measured 426 ms wall clock for this 200 ms step at ~50 %
    // halt duty, 2026-10-17). Timing must therefore be observed halt-free.
    //
    CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_RUNFREE);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

bool board_timer_tick_elapsed(void)
{
    return CPUTimer_getTimerOverflowStatus(CPUTIMER0_BASE);
}

void board_timer_tick_ack(void)
{
    CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);
}

void board_delay_ms(uint32_t ms)
{
    uint32_t cycles = (BOARD_SYSCLK_FREQ_HZ / 1000U) * ms;
    uint32_t count;

    if (cycles <= 9U)
    {
        count = 0U;
    }
    else
    {
        count = (cycles - 9U) / 5U;      // SysCtl_delay() consumes ~5 cycles per count
    }

    SysCtl_delay(count);
}
