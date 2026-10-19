//#############################################################################
//
// FILE:   main.c
//
// TITLE:  LED chaser application entry for LAUNCHXL-F280039C.
//
// DESCRIPTION:
//   Startup sequence: clocks -> GPIO -> SCI -> self test -> chaser loop.
//   When any step fails the firmware enters a fail-safe state (both LEDs blink
//   in unison at 100 ms) instead of pretending to work.
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_clock.h"
#include "board_pins.h"
#include "led_hw.h"
#include "led_pattern.h"
#include "sci_log.h"
#include "self_test.h"
#include "fmt.h"
#include "app_version.h"
#include "driverlib.h"
#include "device.h"

//
// Compile-time configuration (REQ-003: modifying LEDCHASER_STEP_MS scales the
// chaser period linearly).
//
#define LEDCHASER_STEP_MS            200U
#define LEDCHASER_TICK_HZ            1000U     // CPU timer base rate (1 ms)
#define LEDCHASER_HEARTBEAT_MS       5000U
#define LEDCHASER_FAILSAFE_TOGGLE_MS 100U
#define LEDCHASER_MODE_DEFAULT       LED_PATTERN_MODE_CHASE

static void log_label_u32(const char *label, uint32_t value, bool newline);

//
// fail-safe: both LEDs blink in unison, no pattern progression.
//
static void fail_safe_loop(void)
{
    uint32_t mask = 0x3U;

    while (1)
    {
        led_hw_apply(led_pattern_levels(mask, (uint16_t)LEDCHASER_CHANNEL_COUNT,
                                        BOARD_LED_ACTIVE_LOW));
        mask ^= 0x3U;
        board_delay_ms(LEDCHASER_FAILSAFE_TOGGLE_MS);
    }
}

static void log_label_u32(const char *label, uint32_t value, bool newline)
{
    char buf[16];

    sci_log_puts(label);
    if (fmt_u32_dec(value, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    else
    {
        sci_log_puts("<?>");
    }
    if (newline)
    {
        sci_log_puts("\r\n");
    }
}

int main(void)
{
    uint32_t step = 0U;
    uint32_t ms_since_step = 0U;
    uint32_t ms_since_heartbeat = 0U;
    uint32_t mask;
    uint32_t levels;
    self_test_result_t result;

    //
    // 0) .TI.ramfunc relocation. driverlib's SysCtl_delay() is emitted into
    //    .TI.ramfunc (RUN = RAMLS0 = 0x8000) and SysCtl_setClock() calls it,
    //    so this MUST run before any clock configuration - otherwise the CPU
    //    branches into empty RAM, traps, and the boot ROM watchdog-resets the
    //    device. See board_clock.h for the full failure analysis.
    //
    board_ramfunc_relocate();
    if (!board_ramfunc_ready())
    {
        // Console is not up yet, but try anyway: a visible message beats a
        // silent watchdog reset.
        sci_log_init();
        sci_log_line("ERROR: ramfunc relocation FAIL");
        fail_safe_loop();
    }

    //
    // 0b) Watchdog. code_start/wd_disable already clears WDDIS on the power-up
    //     path, but a debugger load can enter directly at main() (CCS auto-run),
    //     skipping codestart - disable here too so no entry path can WDRST-loop.
    //
    SysCtl_disableWatchdog();

    //
    // 0c) Interrupt subsystem. With the PIE disabled, any trap or stray
    //     interrupt is vectored from the boot ROM, whose ITRAP handler enables
    //     the watchdog and resets the device. Enabling the PIE and installing
    //     the driverlib default handlers keeps faults local and debuggable.
    //     No ISRs are registered (the chaser polls the CPU timer), so IER
    //     stays clear and nothing can fire unexpectedly.
    //
    Interrupt_initModule();
    Interrupt_initVectorTable();

    //
    // 1) Clocks. A 120 MHz mismatch is fatal - topology and timing would be off.
    //
    if (!board_clock_init())
    {
        sci_log_init();
        sci_log_line("ERROR: clock FAIL");
        fail_safe_loop();
    }

    //
    // 2) GPIO + SCI console.
    //
    led_hw_init();
    sci_log_init();

    sci_log_line("");
    sci_log_line("=== LAUNCHXL-F280039C LED chaser ===");
    sci_log_line("target : TMS320F280039C / LAUNCHXL-F280039C");
    sci_log_line("version: " APP_VERSION_STRING " (" APP_BUILD_DATE " " APP_BUILD_TIME ")");
    log_label_u32("ramfunc : ", board_ramfunc_size(), false);
    sci_log_line(" bytes relocated");
    log_label_u32("sysclk : ", BOARD_SYSCLK_FREQ_HZ, false);
    sci_log_line(" Hz");
    log_label_u32("step   : ", LEDCHASER_STEP_MS, false);
    sci_log_line(" ms");

    //
    // 3) Power-on self test.
    //
    result = self_test_run();
    if (result == SELF_TEST_OK)
    {
        sci_log_line("self-test: pattern OK");
        sci_log_line("self-test: fmt OK");
    }
    else
    {
        sci_log_line("self-test FAIL");
        log_label_u32("  code=", (uint32_t)result, true);
        fail_safe_loop();
    }

    //
    // 4) Chaser loop, paced by the CPU timer (hardware timer, not a busy wait).
    //
    board_timer_init(LEDCHASER_TICK_HZ);

    while (1)
    {
        if (board_timer_tick_elapsed())
        {
            board_timer_tick_ack();
            ms_since_step++;
            ms_since_heartbeat++;

            if (ms_since_step >= LEDCHASER_STEP_MS)
            {
                ms_since_step = 0U;
                step++;

                mask = led_pattern_mask(LEDCHASER_MODE_DEFAULT,
                                        (uint16_t)LEDCHASER_CHANNEL_COUNT, step);
                levels = led_pattern_levels(mask, (uint16_t)LEDCHASER_CHANNEL_COUNT,
                                            BOARD_LED_ACTIVE_LOW);
                led_hw_apply(levels);
            }

            if (ms_since_heartbeat >= LEDCHASER_HEARTBEAT_MS)
            {
                ms_since_heartbeat = 0U;
                sci_log_puts("alive, steps=");
                log_label_u32("", step, false);
                sci_log_puts(", mode=");
                log_label_u32("", (uint32_t)LEDCHASER_MODE_DEFAULT, true);
            }
        }
    }
}
