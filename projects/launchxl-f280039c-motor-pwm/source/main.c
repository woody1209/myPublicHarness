//#############################################################################
//
// FILE:   main.c
//
// TITLE:  Motor PWM skeleton application entry for LAUNCHXL-F280039C.
//
// DESCRIPTION:
//   Open-loop demo: fixed-point electrical angle -> SVPWM -> three-phase
//   complementary PWM (ePWM1/2/3, dead-band), with an ADC sample of the
//   ISENSE pin and a CMPSS over-current trip chain that one-shot latches all
//   six outputs low. NO motor is attached; the goal is to observe the
//   algorithm and the PWM waveforms.
//
//   Startup sequence (ADR 0003, every step load-bearing):
//     ramfunc relocate -> watchdog off -> PIE init -> clocks ->
//     LEDs + SCI -> ePWM verify -> ADC -> CMPSS probe -> TZ arm ->
//     pure-logic self test -> superloop
//
//   Any board-level failure enters the fail-safe state (both LEDs blink in
//   unison at 100 ms) instead of pretending to work (REQ-008).
//
// LICENSE: BSD-3-Clause style, consistent with TI C2000Ware driverlib.
//
//#############################################################################

#include "board_clock.h"
#include "board_pins.h"
#include "board_epwm.h"
#include "board_adc.h"
#include "board_cmpss.h"
#include "sci_log.h"
#include "self_test.h"
#include "angle_gen.h"
#include "svpwm.h"
#include "fmt.h"
#include "app_version.h"
#include "driverlib.h"
#include "device.h"

//
// Compile-time configuration (spec 0002 design.md SCI contract).
//
#define MOTORPWM_RATE_DEFAULT_X100   500U      //!< 5.00 Hz electrical
#define MOTORPWM_MAG_DEFAULT_PERMILLE 800U     //!< over-modulating on purpose:
                                               //!< exercises the SVPWM guard
#define MOTORPWM_TICK_HZ             1000U     //!< CPU timer base (1 ms)
#define MOTORPWM_MOTOR_PERIOD_MS     100U      //!< motor: line at 10 Hz
#define MOTORPWM_ALIVE_PERIOD_MS     1000U     //!< alive line at 1 Hz
#define MOTORPWM_FAULT_RECOVER_MS    5000U     //!< TZ fault dwell before
                                               //!< software clears the latch
#define MOTORPWM_FAULT_BLINK_MS      500U      //!< LED4 slow blink in fault
#define MOTORPWM_FAILSAFE_TOGGLE_MS  100U      //!< both LEDs in unison

//
// LED semantics (active-low, board_pins.h truth):
//   LED5 (green) solid            - running
//   LED4 (red)   500 ms blink     - trip-zone fault (outputs forced low)
//   both LEDs 100 ms unison       - fail-safe (self test failed)
//
static void motor_pwm_led4(bool on)
{
    GPIO_writePin(BOARD_LED4_GPIO, on ? 0U : 1U);
}

static void motor_pwm_led5(bool on)
{
    GPIO_writePin(BOARD_LED5_GPIO, on ? 0U : 1U);
}

static void motor_pwm_led_init(void)
{
    GPIO_setPinConfig(BOARD_LED4_PINCFG);
    GPIO_setPinConfig(BOARD_LED5_PINCFG);
    GPIO_setDirectionMode(BOARD_LED4_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setDirectionMode(BOARD_LED5_GPIO, GPIO_DIR_MODE_OUT);
    motor_pwm_led4(false);
    motor_pwm_led5(false);
}

//
// fail-safe: both LEDs blink in unison, angle parked, nothing printed.
//
static void fail_safe_loop(void)
{
    bool on = false;

    for (;;)
    {
        on = !on;
        motor_pwm_led4(on);
        motor_pwm_led5(on);
        board_delay_ms(MOTORPWM_FAILSAFE_TOGGLE_MS);
    }
}

static void log_u32(uint32_t value)
{
    char buf[16];

    if (fmt_u32_dec(value, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    else
    {
        sci_log_puts("<?>");
    }
}

static void log_field(const char *label, uint32_t value)
{
    sci_log_puts(label);
    log_u32(value);
}

//
// CMPSS bidirectional probe (REQ-004/REQ-008): the high comparator trips
// when CMPIN1P (A2) is ABOVE the DAC reference. The trip direction puts the
// reference 512 codes BELOW the measured input (or at 0 when the baseline
// is already low); the no-trip direction puts it at FULL SCALE. The TZ
// chain is NOT armed yet, so a trip here is harmless. Console contract
// (REQ-007): "cmpss: adc=<baseline> hi=1 lo=0 OK" - the baseline the two
// directions were referenced to, then the COMPARATOR STATUS in each
// direction.
//
// WHY full scale and not "512 codes above the baseline" for the no-trip
// direction (2026-10-25, on target): A2 floats on this board - no power
// stage is attached - and the ePWM carrier is already switching on the
// neighbouring pins during the probe, so the floating input picks up noise
// spikes that exceed a 512-code window. Measured: the same ~39-code
// baseline passed at 10:16 and failed at 10:23 (lo=1) - the window was the
// flaky part, not the chain. A VDDA-referenced full-scale threshold sits
// above ANY voltage the pin can carry, which makes the direction
// deterministic; a stuck-high comparator or a dead DAC still fails it.
//
static bool board_cmpss_probe(void)
{
    uint16_t isense;
    uint16_t trip_dac;
    bool hi_ok;
    bool lo_ok;
    bool hi_status;
    bool lo_status;

    isense = board_adc_read_raw_blocking();        // 0xFFFF = ADC timeout
    trip_dac = (isense > 512U) ? (uint16_t)(isense - 512U) : 0U;

    //
    // Trip direction: reference below the input, wait (bounded) for the
    // comparator to follow. Record the actual comparator status for the
    // console contract line (REQ-007), not the wait-match flag.
    //
    board_cmpss_set_dac(trip_dac);
    hi_ok = board_cmpss_wait_status(true, BOARD_CMPSS_SETTLE_TIMEOUT_MS);
    hi_status = board_cmpss_comp_status();

    //
    // Clear the sticky filter latch before the no-trip direction so each
    // direction reads a fresh comparison (diag_dac.js 2026-10-25: bit1
    // stayed set and contaminated the second read otherwise).
    //
    board_cmpss_clear_filter_latch();

    //
    // No-trip direction: reference at full scale (see the header comment).
    //
    board_cmpss_set_dac(BOARD_CMPSS_DAC_MAX);
    lo_ok = board_cmpss_wait_status(false, BOARD_CMPSS_SETTLE_TIMEOUT_MS);
    lo_status = board_cmpss_comp_status();

    //
    // Park the reference at the maximum again: the X-BAR/TZ wiring is about
    // to be armed and a floating A2 must not be able to assert CTRIPH.
    //
    board_cmpss_set_dac(BOARD_CMPSS_DAC_MAX);

    //
    // Console contract (REQ-007): "cmpss: adc=<baseline> hi=<st> lo=<st>
    // OK" - the ADC baseline the two directions were referenced to, then
    // the COMPARATOR STATUS at each direction (hi: 1 = tripped as expected,
    // lo: 0 = stayed low as expected). The adc= field makes a wrong
    // baseline (e.g. a pre-settle reference) visible in the evidence log
    // instead of silently producing hi=0 lo=0.
    //
    // board_cmpss_wait_status(expect) returns TRUE when the status MATCHES
    // expect, NOT the status value itself. The probe passes when BOTH
    // directions reached their expectation (hi_ok && lo_ok).
    //
    sci_log_puts("cmpss: adc=");
    log_u32(isense);
    sci_log_puts(" hi=");
    log_u32(hi_status ? 1U : 0U);
    sci_log_puts(" lo=");
    log_u32(lo_status ? 1U : 0U);
    if (hi_ok && lo_ok)
    {
        sci_log_line(" OK");
        return true;
    }
    sci_log_line(" FAIL");
    return false;
}

int main(void)
{
    angle_gen_t angle;
    svpwm_duties_t duties;
    uint32_t ticks = 0U;
    uint32_t ms_since_motor = 0U;
    uint32_t ms_since_alive = 0U;
    uint32_t ms_since_blink = 0U;
    uint32_t fault_ms = 0U;
    bool blink_phase = false;
    bool fault_active = false;
    bool fault_reported = false;
    uint16_t prd;
    uint16_t db;
    uint16_t pol;
    self_test_result_t st_result;

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
        sci_log_init();
        sci_log_line("ERROR: ramfunc relocation FAIL");
        fail_safe_loop();
    }

    //
    // 0b) Watchdog. code_start/wd_disable already clears WDDIS on the
    //     power-up path, but a debugger load can enter directly at main()
    //     (CCS auto-run), skipping codestart - disable here too so no entry
    //     path can WDRST-loop.
    //
    SysCtl_disableWatchdog();

    //
    // 0c) Interrupt subsystem. With the PIE disabled, any trap or stray
    //     interrupt is vectored from the boot ROM, whose ITRAP handler
    //     enables the watchdog and resets the device. Enabling the PIE and
    //     installing the driverlib default handlers keeps faults local and
    //     debuggable. No ISRs are registered (this firmware polls), so IER
    //     stays clear and nothing can fire unexpectedly.
    //
    Interrupt_initModule();
    Interrupt_initVectorTable();

    //
    // 1) Clocks. A 120 MHz mismatch is fatal - the PWM carrier, the dead-band
    //    time and the ADC sample window are all derived from SYSCLK.
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
    motor_pwm_led_init();
    sci_log_init();

    sci_log_line("");
    sci_log_line("=== LAUNCHXL-F280039C motor-pwm ===");
    sci_log_line("target : TMS320F280039C / LAUNCHXL-F280039C");
    sci_log_line("version: " APP_VERSION_STRING " (" APP_BUILD_DATE " " APP_BUILD_TIME ")");
    sci_log_line("note   : no motor attached - open-loop angle/SVPWM demo");
    log_field("sysclk : ", BOARD_SYSCLK_FREQ_HZ);
    sci_log_line(" Hz");
    log_field("rate   : ", MOTORPWM_RATE_DEFAULT_X100);
    sci_log_line(" x0.01 Hz");
    log_field("mag    : ", MOTORPWM_MAG_DEFAULT_PERMILLE);
    sci_log_line(" permille");

    //
    // 3) ePWM: configure, then read the registers back before anything
    //    depends on them (REQ-002).
    //
    board_epwm_init();
    board_epwm_report(&prd, &db, &pol);
    sci_log_puts("pwm: prd=");
    log_u32(prd);
    sci_log_puts(" db=");
    log_u32(db);
    sci_log_puts(" pol=");
    log_u32(pol);
    if (board_epwm_verify())
    {
        sci_log_line(" OK");
    }
    else
    {
        sci_log_line(" FAIL");
        fail_safe_loop();
    }

    //
    // 4) ADC: SOC0 <- ePWM1 SOCA on the ISENSE pin (REQ-003).
    //
    board_adc_init();

    //
    // 5) CMPSS: configure, prove the comparator responds both ways, and only
    //    then arm the trip chain (REQ-004).
    //
    board_cmpss_init();
    if (!board_cmpss_probe())
    {
        fail_safe_loop();
    }
    board_cmpss_arm_tz();
    sci_log_line("tz: chain armed");

    //
    // 6) Pure-logic self test (same vectors the host tests assert).
    //
    st_result = self_test_run();
    if (st_result == SELF_TEST_OK)
    {
        sci_log_line("self-test: svpwm/angle/fmt OK");
    }
    else
    {
        sci_log_line("self-test FAIL");
        log_field("  code=", (uint32_t)st_result);
        fail_safe_loop();
    }

    //
    // 7) Superloop, paced by the CPU timer (hardware timer, not a busy
    //    wait). Angle -> SVPWM -> CMPA at 10 Hz; liveness line at 1 Hz;
    //    trip-zone latch polled every tick.
    //
    angle_gen_init(&angle, MOTORPWM_RATE_DEFAULT_X100);
    board_timer_init(MOTORPWM_TICK_HZ);
    motor_pwm_led5(true);

    for (;;)
    {
        if (!board_timer_tick_elapsed())
        {
            continue;
        }
        board_timer_tick_ack();
        ticks++;

        //
        // Trip-zone fault state: the hardware already forced all six outputs
        // low. Park the angle, blink LED4, and recover only after the dwell
        // AND the comparator is back below the reference.
        // NOTE: the latch itself (TZFLG.DCBEVT1) can ONLY be cleared by
        // clear_tz() - it never self-clears - so the recovery condition must
        // NOT require "not latched" (that would deadlock: clear_tz waits for
        // a condition only clear_tz can produce). The comparator returning
        // below the reference is the real "over-current gone" signal.
        //
        if (fault_active)
        {
            fault_ms++;
            ms_since_blink++;
            if (ms_since_blink >= MOTORPWM_FAULT_BLINK_MS)
            {
                ms_since_blink = 0U;
                blink_phase = !blink_phase;
                motor_pwm_led4(blink_phase);
            }
            if ((fault_ms >= MOTORPWM_FAULT_RECOVER_MS) &&
                !board_cmpss_comp_status())
            {
                board_cmpss_clear_tz();
                fault_active = false;
                fault_reported = false;
                fault_ms = 0U;
                motor_pwm_led4(false);
                motor_pwm_led5(true);
                sci_log_line("tz: cleared");
            }
            continue;
        }

        if (board_cmpss_tz_latched())
        {
            fault_active = true;
            fault_ms = 0U;
            ms_since_blink = 0U;
            motor_pwm_led5(false);
            if (!fault_reported)
            {
                fault_reported = true;
                sci_log_line("FAULT: tz-latched");
            }
            continue;
        }

        ms_since_motor++;
        ms_since_alive++;

        if (ms_since_motor >= MOTORPWM_MOTOR_PERIOD_MS)
        {
            ms_since_motor = 0U;

            //
            // Advance by the real elapsed time, modulate, and push the
            // duties to the three modules.
            //
            (void)angle_gen_advance(&angle, MOTORPWM_MOTOR_PERIOD_MS * 1000U);
            if (svpwm_modulate((uint16_t)angle_gen_get(&angle),
                               MOTORPWM_MAG_DEFAULT_PERMILLE, &duties) ==
                SVPWM_OK)
            {
                board_epwm_set_duties(duties.a, duties.b, duties.c);
            }

            sci_log_puts("motor: ang=");
            log_u32(angle_gen_get(&angle));
            sci_log_puts(" mag=");
            log_u32(MOTORPWM_MAG_DEFAULT_PERMILLE);
            sci_log_puts(" a=");
            log_u32(duties.a);
            sci_log_puts(" b=");
            log_u32(duties.b);
            sci_log_puts(" c=");
            log_u32(duties.c);
            sci_log_puts(" adc=");
            log_u32(board_adc_read_latest());
            sci_log_puts("\r\n");
        }

        if (ms_since_alive >= MOTORPWM_ALIVE_PERIOD_MS)
        {
            ms_since_alive = 0U;
            sci_log_puts("alive, ticks=");
            log_u32(ticks);
            sci_log_puts("\r\n");
        }
    }
}
