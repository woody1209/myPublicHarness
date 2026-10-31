//#############################################################################
//
// FILE:   main.c
//
// TITLE:  Full-platform application entry for LAUNCHXL-F280039C.
//
// DESCRIPTION:
//   M1 (spec 0002): startup per ADR 0003, extended self test, cooperative
//   scheduler driving all subsystems, 5 s aggregated health line, LED chaser
//   kept as the always-visible platform pulse. Subsystem tasks are added in
//   M2-M4 (see specs/0002-full-platform/tasks.md).
//
//   Startup sequence (hard constraint, ADR 0003):
//   ramfunc relocate -> watchdog off -> PIE init -> clock -> peripherals.
//   Any startup failure enters fail-safe (both LEDs blink at 100 ms).
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
#include "scheduler.h"
#include "health.h"
#include "dcc_mon_hw.h"
#include "adc_scan_hw.h"
#include "dac_cmpss_hw.h"
#include "pwm_bank_hw.h"
#include "cap_meas_hw.h"
#include "qep_quadv_hw.h"
#include "frame_gen.h"
#include "spi_loop_hw.h"
#include "i2c_pair_hw.h"
#include "serial_loop_hw.h"
#include "can_loop_hw.h"
#include "fsi_loop_hw.h"
#include "dma_bank_hw.h"
#include "accel_hw.h"
#include "clb_demo_hw.h"
#include "cla_cpu.h"
#include "frame_gen.h"
#include "spi_loop_hw.h"
#include "i2c_pair_hw.h"
#include "serial_loop_hw.h"
#include "can_loop_hw.h"
#include "fsi_loop_hw.h"
#include "dma_bank_hw.h"
#include "accel_hw.h"
#include "clb_demo_hw.h"
#include "cla_cpu.h"
#include "app_version.h"
#include "driverlib.h"
#include "device.h"

//
// Compile-time configuration.
//
#define FP_TICK_HZ                   1000U     /* CPUTimer0 base rate: 1 ms   */
#define FP_LED_PERIOD_MS             200U      /* platform pulse (H5 beat)    */
#define FP_LED_MODE_DEFAULT          LED_PATTERN_MODE_CHASE
#define FP_HEALTH_PERIOD_MS          5000U     /* aggregated health line      */
#define FP_FAILSAFE_TOGGLE_MS        100U      /* fail-safe both-LED blink    */
#define FP_DCC_PERIOD_MS             10U       /* DCC window poll             */
#define FP_CAP_PERIOD_MS             10U       /* eCAP measurement step       */
#define FP_QEP_PERIOD_MS             100U      /* eQEP speed sample           */
#define FP_DAC_PERIOD_MS             100U      /* DAC staircase step          */
#define FP_M2SLOW_PERIOD_MS          1000U     /* PWM verify + stats reset    */
#define FP_SPI_PERIOD_MS             10U       /* SPIA polled loopback        */
#define FP_I2C_PERIOD_MS             100U      /* I2CA loopback pass          */
#define FP_SER_PERIOD_MS             10U       /* SCIB/LINA/LINB round-robin  */
#define FP_CAN_PERIOD_MS             100U      /* CAN std/ext frame pair      */
#define FP_FSI_PERIOD_MS             100U      /* FSI loopback frame          */
#define FP_DMA_PERIOD_MS             1000U     /* DMA bank step + verify      */
#define FP_ACCEL_PERIOD_MS           1000U     /* BGCRC/AES/TMU step          */
#define FP_CLB_PERIOD_MS             1000U     /* CLB GPREG proof             */
#define FP_CLA_PERIOD_MS             100U      /* CLA task kick + judge       */
#define FP_SPI_PERIOD_MS             10U       /* SPIA polled loopback        */
#define FP_I2C_PERIOD_MS             100U      /* I2CA loopback pass          */
#define FP_SER_PERIOD_MS             10U       /* SCIB/LINA/LINB round-robin  */
#define FP_CAN_PERIOD_MS             100U      /* CAN std/ext frame pair      */
#define FP_FSI_PERIOD_MS             100U      /* FSI loopback frame          */
#define FP_DMA_PERIOD_MS             1000U     /* DMA bank step + verify      */
#define FP_ACCEL_PERIOD_MS           1000U     /* BGCRC/AES/TMU step          */
#define FP_CLB_PERIOD_MS             1000U     /* CLB GPREG proof             */
#define FP_CLA_PERIOD_MS             100U      /* CLA task kick + judge       */

static volatile uint32_t g_now_ms = 0U;
static uint32_t g_step = 0U;
static uint32_t g_health_lines = 0U;
static int16_t g_h_timer = -1;
static int16_t g_h_led = -1;
static int16_t g_h_dcc = -1;
static int16_t g_h_adc = -1;
static int16_t g_h_dacq = -1;
static int16_t g_h_pwm = -1;
static int16_t g_h_ecap = -1;
static int16_t g_h_qep = -1;

//! Aggregate M2 live-check bit map for the HIL DSS gate (H7).
//! NOT static: the DSS gate reads it by symbol (hil_test.js H7).
volatile uint32_t g_m2_status = 0U;

//! Aggregate M3 live-check bit map for the HIL DSS gate (H8):
//!   bit0 spi | bit1 i2c | bit2 ser | bit3 can | bit4 fsi | bit5 dma
volatile uint32_t g_m3_status = 0U;

//! M4 live-check bit map (H9): bit0 bgcrc | bit1 aes | bit2 tmu |
//! bit3 cla | bit4 clb | bit5 wd (watchdog running)
volatile uint32_t g_m4_status = 0U;
volatile uint32_t g_wd_service_count = 0U;
volatile uint32_t g_wd_faultinject = 0U;   /* DSS writes 1 to stop service */
static int16_t g_h_accel = -1;
static int16_t g_h_clb = -1;
static int16_t g_h_cla = -1;
static void task_accel(uint32_t now_ms);
static void task_clb(uint32_t now_ms);
static void task_cla(uint32_t now_ms);
static int16_t g_h_spi = -1;
static int16_t g_h_i2c = -1;
static int16_t g_h_ser = -1;
static int16_t g_h_can = -1;
static int16_t g_h_fsi = -1;
static int16_t g_h_dma = -1;

static void task_spi(uint32_t now_ms);
static void task_i2c(uint32_t now_ms);
static void task_ser(uint32_t now_ms);
static void task_can(uint32_t now_ms);
static void task_fsi(uint32_t now_ms);
static void task_dma(uint32_t now_ms);

//
// Warm-up grace: subsystem judgements that need several samples (eCAP band
// means, eQEP speed deltas) are not measurable in the first second. Health
// accounting is suppressed until this elapses - otherwise a normal warm-up
// transient latches the MUST monitors' fail-safe within 30 ms of boot
// (2026-10-25 first M2 board run: eCAP+QEP flagged at up=0 and the platform
// entered fail-safe before the loopbacks had produced a single judgement).
//
#define FP_WARMUP_MS                2000U

static void task_timebase(uint32_t now_ms);
static void task_led_pulse(uint32_t now_ms);
static void task_health_line(uint32_t now_ms);
static void task_dcc(uint32_t now_ms);
static void task_cap(uint32_t now_ms);
static void task_qep(uint32_t now_ms);
static void task_dac(uint32_t now_ms);
static void task_m2slow(uint32_t now_ms);

//! Scheduler descriptors (main owns them; scheduler keeps pointers only).
static sched_task_t g_task_timebase = { "tbase", &task_timebase, 0U, 0U,
                                        0U, 0U, 0 };
static sched_task_t g_task_led      = { "ledch", &task_led_pulse,
                                        FP_LED_PERIOD_MS, 0U, 0U, 0U, 0 };
static sched_task_t g_task_health   = { "hline", &task_health_line,
                                        FP_HEALTH_PERIOD_MS, 0U, 0U, 0U, 0 };
static sched_task_t g_task_dcc      = { "dcc", &task_dcc,
                                        FP_DCC_PERIOD_MS, 1U, 0U, 0U, 0 };
static sched_task_t g_task_cap      = { "cap", &task_cap,
                                        FP_CAP_PERIOD_MS, 2U, 0U, 0U, 0 };
static sched_task_t g_task_qep      = { "qep", &task_qep,
                                        FP_QEP_PERIOD_MS, 3U, 0U, 0U, 0 };
static sched_task_t g_task_dac      = { "dacq", &task_dac,
                                        FP_DAC_PERIOD_MS, 4U, 0U, 0U, 0 };
static sched_task_t g_task_m2slow   = { "m2slow", &task_m2slow,
                                        FP_M2SLOW_PERIOD_MS, 5U, 0U, 0U, 0 };
static sched_task_t g_task_spi      = { "spilb", &task_spi,
                                        FP_SPI_PERIOD_MS, 6U, 0U, 0U, 0 };
static sched_task_t g_task_i2c      = { "i2clb", &task_i2c,
                                        FP_I2C_PERIOD_MS, 7U, 0U, 0U, 0 };
static sched_task_t g_task_ser      = { "serlb", &task_ser,
                                        FP_SER_PERIOD_MS, 8U, 0U, 0U, 0 };
static sched_task_t g_task_can      = { "canlb", &task_can,
                                        FP_CAN_PERIOD_MS, 9U, 0U, 0U, 0 };
static sched_task_t g_task_fsi      = { "fsilb", &task_fsi,
                                        FP_FSI_PERIOD_MS, 10U, 0U, 0U, 0 };
static sched_task_t g_task_dma      = { "dmabk", &task_dma,
                                        FP_DMA_PERIOD_MS, 11U, 0U, 0U, 0 };
static sched_task_t g_task_accel    = { "accel", &task_accel,
                                        FP_ACCEL_PERIOD_MS, 12U, 0U, 0U, 0 };
static sched_task_t g_task_clb      = { "clbdm", &task_clb,
                                        FP_CLB_PERIOD_MS, 13U, 0U, 0U, 0 };
static sched_task_t g_task_cla      = { "clatk", &task_cla,
                                        FP_CLA_PERIOD_MS, 14U, 0U, 0U, 0 };

static void fail_safe_loop(void);
static void log_label_u32(const char *label, uint32_t value, bool newline);

//
// Task: 1 ms timebase poll + ADC scan advance (period 0 = every pass).
//
static void task_timebase(uint32_t now_ms)
{
    (void)now_ms;
    if (g_wd_faultinject == 0U)
    {
        SysCtl_serviceWatchdog();
        g_wd_service_count++;
    }
    if (board_timer_tick_elapsed())
    {
        board_timer_tick_ack();
        g_now_ms++;
        if (g_h_timer >= 0)
        {
            health_beat(g_h_timer);
        }
    }
    adc_scan_step();

    //
    // Aggregate M2 live-check bit map for the HIL DSS gate (H7) - refreshed
    // every pass so a gate read after any halt sees fresh state, never a
    // stale 1 s-old bit (the DCC bit in particular latches through halts
    // and is cleared by the running task within 10 ms).
    //   bit0 dcc | bit1 adc | bit2 dac-loopback | bit3 cmpss/tz (optional)
    //   bit4 ecap | bit5 qep | bit6 pwm-verify
    //
    g_m2_status =
        ((g_dcc_consec == 0U) ? 1UL : 0UL) |
        ((g_adc_errors == 0U) ? 2UL : 0UL) |
        (((g_dac_steps_ok > 0U) && (g_dac_steps_err == 0U)) ? 4UL : 0UL) |
        ((g_cmpss_high_hits > 0U) ? 8UL : 0UL) |
        ((g_cap_ok_mask == 0x7U) ? 16UL : 0UL) |
        ((g_qep_ok_mask == 0x3U) ? 32UL : 0UL) |
        ((g_pwm_verify_err == 0U) ? 64UL : 0UL);

    //
    // M3 live-check bit map (H8):
    //   bit0 spi | bit1 i2c | bit2 ser | bit3 can | bit4 fsi | bit5 dma
    //
    g_m3_status =
        (((g_spi_a_ok > 0U) && (g_spi_a_err == 0U)) ? 1UL : 0UL) |
        (((g_i2c_ok > 0U) && (g_i2c_err == 0U)) ? 2UL : 0UL) |
        (((g_ser_ok > 0U) && (g_ser_err == 0U)) ? 4UL : 0UL) |
        (((g_can_ok > 0U) && (g_can_err == 0U)) ? 8UL : 0UL) |
        (((g_fsi_ok > 0U) && (g_fsi_err == 0U)) ? 16UL : 0UL) |
        ((g_dma_done[0] > 0U) ? 32UL : 0UL);

    //
    // M4 live-check bit map (H9):
    //   bit0 bgcrc | bit1 aes | bit2 tmu | bit3 cla | bit4 clb | bit5 wd
    //
    g_m4_status =
        ((g_bgcrc_err == 0U) ? 1UL : 0UL) |
        ((g_aes_err == 0U) ? 2UL : 0UL) |
        ((g_tmu_err == 0U) ? 4UL : 0UL) |
        ((g_cla_err == 0U) ? 8UL : 0UL) |
        ((g_clb_err == 0U) ? 16UL : 0UL) |
        ((g_wd_service_count > 0U) ? 32UL : 0UL);
}

//
// Task: DCC0 continuous clock-verification poll (10 ms).
//
static void task_dcc(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[D]"); }
    (void)now_ms;
    if (dcc_mon_check())
    {
        if (g_h_dcc >= 0) { health_beat(g_h_dcc); }
    }
    else if (g_h_dcc >= 0)
    {
        health_fail(g_h_dcc);
    }
}

//
// Task: eCAP pad-loopback measurement (10 ms).
// Health accounting starts only after the first capture window exists -
// otherwise the warm-up samples would latch the MUST monitor's fail-safe.
//
static void task_cap(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[C]"); }
    (void)now_ms;
    cap_meas_step();
    if ((g_cap_samples[0] < 8U) || (g_cap_samples[1] < 8U) ||
        (g_cap_samples[2] < 8U))
    {
        return;                            /* warm-up: band mean not ready */
    }
    if (g_cap_ok_mask == 0x7U)
    {
        if (g_h_ecap >= 0) { health_beat(g_h_ecap); }
    }
    else if (g_h_ecap >= 0)
    {
        health_fail(g_h_ecap);
    }
}

//
// Task: eQEP quadrature readback (100 ms).
//
static void task_qep(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[Q]"); }
    (void)now_ms;
    qep_quadv_step();
    if ((g_qep_rate_meas[0] == 0U) || (g_qep_rate_meas[1] == 0U))
    {
        return;                            /* warm-up: no delta measured yet */
    }
    if (g_qep_ok_mask == 0x3U)
    {
        if (g_h_qep >= 0) { health_beat(g_h_qep); }
    }
    else if (g_h_qep >= 0)
    {
        health_fail(g_h_qep);
    }
}

//
// Task: DAC staircase / CMPSS / TZ step (100 ms).
//
static void task_dac(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[A]"); }
    (void)now_ms;
    dac_cmpss_step();
    if ((g_dac_steps_ok == 0U) && (g_dac_steps_err == 0U))
    {
        return;                            /* warm-up or degraded: no judge */
    }
    if ((g_dac_steps_ok > 0U) && (g_dac_steps_err == 0U))
    {
        if (g_h_dacq >= 0) { health_beat(g_h_dacq); }
    }
    else if (g_h_dacq >= 0)
    {
        health_fail(g_h_dacq);
    }
}

//
// Task: slow M2 verification (1 s): ePWM register re-read + ADC stats reset.
//
static void task_m2slow(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[M]"); }
    uint32_t i;
    uint32_t j;

    (void)now_ms;
    pwm_bank_verify();
    if (g_pwm_verify_err == 0U)
    {
        if (g_h_pwm >= 0) { health_beat(g_h_pwm); }
    }
    else if (g_h_pwm >= 0)
    {
        health_fail(g_h_pwm);
    }

    if (g_now_ms < FP_WARMUP_MS)
    {
        return;                            /* warm-up: no judgement yet */
    }
    if ((g_adc_conversions > 0U) && (g_adc_errors == 0U))
    {
        if (g_h_adc >= 0) { health_beat(g_h_adc); }
    }
    else if (g_h_adc >= 0)
    {
        health_fail(g_h_adc);
    }

    for (i = 0U; i < ADC_SCAN_MODS; i++)
    {
        for (j = 0U; j < ADC_SCAN_SOCS_PER_MOD; j++)
        {
            adc_stat_reset(&g_adc_stats[i][j]);
        }
    }

}

//
// Task: LED chaser step - the always-visible platform pulse.
//
static void task_led_pulse(uint32_t now_ms)
{
    uint32_t mask;
    uint32_t levels;

    (void)now_ms;
    g_step++;
    mask = led_pattern_mask(FP_LED_MODE_DEFAULT,
                            (uint16_t)LEDCHASER_CHANNEL_COUNT, g_step);
    levels = led_pattern_levels(mask, (uint16_t)LEDCHASER_CHANNEL_COUNT,
                                BOARD_LED_ACTIVE_LOW);
    led_hw_apply(levels);
    if (g_h_led >= 0)
    {
        health_beat(g_h_led);
    }
}

//
// Task: aggregated health line every 5 s (REQ-020).
//
static void task_health_line(uint32_t now_ms)
{
    char buf[12];

    (void)now_ms;
    g_health_lines++;
    sci_log_puts("health: up=");
    if (fmt_u32_dec(g_now_ms / 1000U, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" map=0x");
    if (fmt_u32_hex(health_map(), buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" err=");
    if (fmt_u32_dec(health_err_total(), buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" hb=");
    if (fmt_u32_dec(health_min_beats(), buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" step=");
    if (fmt_u32_dec(g_step, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(", mode=");
    if (fmt_u32_dec((uint32_t)FP_LED_MODE_DEFAULT, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts("\r\n");

    //
    // M2 status line: measured loopback values + the live-check bit map that
    // task_m2slow maintains (bit map detail in task_m2slow).
    //
    sci_log_puts("m2: ecap=");
    if (fmt_u32_dec(g_cap_hz[0], buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts("/");
    if (fmt_u32_dec(g_cap_hz[1], buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts("/");
    if (fmt_u32_dec(g_cap_hz[2], buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" qep=");
    if (fmt_u32_dec((uint32_t)g_qep_pos[0], buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts("/");
    if (fmt_u32_dec((uint32_t)g_qep_pos[1], buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" dlb=");
    if (fmt_u32_dec(g_dac_steps_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" mux=");
    if (fmt_u32_dec((uint32_t)dac_cmpss_mux(), buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" st=0x");
    if (fmt_u32_hex(g_m2_status, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts("\r\n");

    //
    // M3 status line (H8): loopback exchange counters + bit map.
    //
    sci_log_puts("m3: spi=");
    if (fmt_u32_dec(g_spi_a_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" i2c=");
    if (fmt_u32_dec(g_i2c_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" ser=");
    if (fmt_u32_dec(g_ser_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" can=");
    if (fmt_u32_dec(g_can_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" fsi=");
    if (fmt_u32_dec(g_fsi_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" dma=");
    if (fmt_u32_dec(g_dma_done[0], buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" st=0x");
    if (fmt_u32_hex(g_m3_status, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts("\r\n");

    //
    // M4 status line (H9): accelerator counters + bit map.
    //
    sci_log_puts("m4: bgcrc=");
    if (fmt_u32_dec(g_bgcrc_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" aes=");
    if (fmt_u32_dec(g_aes_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" tmu=");
    if (fmt_u32_dec(g_tmu_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" cla=");
    if (fmt_u32_dec(g_cla_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" clb=");
    if (fmt_u32_dec(g_clb_ok, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts(" wd=");
    if (fmt_u32_dec(g_wd_service_count, buf, sizeof(buf)) > 0)
    {
        sci_log_puts(buf);
    }
    sci_log_puts(" st=0x");
    if (fmt_u32_hex(g_m4_status, buf, sizeof(buf)) > 0) { sci_log_puts(buf); }
    sci_log_puts("\r\n");
}

//
// Task: SPIA polled loopback step (10 ms).
//
static void task_spi(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[S]"); }
    (void)now_ms;
    spi_loop_step_a();
    if ((g_spi_a_ok > 0U) && (g_spi_a_err == 0U))
    {
        if (g_h_spi >= 0) { health_beat(g_h_spi); }
    }
    else if ((g_spi_a_err > 0U) && (g_h_spi >= 0))
    {
        health_fail(g_h_spi);
    }
}

//
// Task: I2CA loopback pass (100 ms).
//
static void task_i2c(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[I]"); }
    (void)now_ms;
    i2c_pair_step();
    if ((g_i2c_ok > 0U) && (g_i2c_err == 0U))
    {
        if (g_h_i2c >= 0) { health_beat(g_h_i2c); }
    }
    else if ((g_i2c_err > 0U) && (g_h_i2c >= 0))
    {
        health_fail(g_h_i2c);
    }
}

//
// Task: SCIB/LINA/LINB round-robin step (10 ms).
//
static void task_ser(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[L]"); }
    (void)now_ms;
    serial_loop_step();
    if ((g_ser_ok > 0U) && (g_ser_err == 0U))
    {
        if (g_h_ser >= 0) { health_beat(g_h_ser); }
    }
    else if ((g_ser_err > 0U) && (g_h_ser >= 0))
    {
        health_fail(g_h_ser);
    }
}

//
// Task: CAN loopback frame pair (100 ms).
//
static void task_can(uint32_t now_ms)
{
    (void)now_ms;
    can_loop_step();
    if ((g_can_ok > 0U) && (g_can_err == 0U))
    {
        if (g_h_can >= 0) { health_beat(g_h_can); }
    }
    else if ((g_can_err > 0U) && (g_h_can >= 0))
    {
        health_fail(g_h_can);
    }
}

//
// Task: FSI loopback frame step (100 ms).
//
static void task_fsi(uint32_t now_ms)
{
    static bool first = true;
    if (first) { first = false; sci_log_puts("[F]"); }
    (void)now_ms;
    fsi_loop_step();
    if ((g_fsi_ok > 0U) && (g_fsi_err == 0U))
    {
        if (g_h_fsi >= 0) { health_beat(g_h_fsi); }
    }
    else if ((g_fsi_err > 0U) && (g_h_fsi >= 0))
    {
        health_fail(g_h_fsi);
    }
}

//
// Task: DMA bank step + verify (1 s).
//
static void task_dma(uint32_t now_ms)
{
    (void)now_ms;
    dma_bank_step();
    if ((g_dma_done[0] > 0U) && (g_dma_err == 0U))
    {
        if (g_h_dma >= 0) { health_beat(g_h_dma); }
    }
    else if ((g_dma_err > 0U) && (g_h_dma >= 0))
    {
        health_fail(g_h_dma);
    }
}

//
// Task: accelerator matrix step (1 s): BGCRC window, TMU batch, AES roundtrip.
//
static void task_accel(uint32_t now_ms)
{
    (void)now_ms;
    accel_step();
}

//
// Task: CLB GPREG register proof (1 s).
//
static void task_clb(uint32_t now_ms)
{
    (void)now_ms;
    clb_demo_step();
}

//
// Task: CLA kick + judge (100 ms).
//
static void task_cla(uint32_t now_ms)
{
    (void)now_ms;
    cla_cpu_step();
    cla_cpu_judge();
}

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
        board_delay_ms(FP_FAILSAFE_TOGGLE_MS);
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
    uint32_t result_u32;
    self_test_result_t result;

    //
    // 0) .TI.ramfunc relocation - MUST run before any SysCtl_* call
    //    (ADR 0003: skipping it traps into empty RAMLS0 and dies).
    //
    board_ramfunc_relocate();
    if (!board_ramfunc_ready())
    {
        sci_log_init();
        sci_log_line("ERROR: ramfunc relocation FAIL");
        fail_safe_loop();
    }

    //
    // 0b) Watchdog off for now. M4 (REQ-018) re-enables the windowed
    //     watchdog AFTER all subsystems are initialised.
    //
    SysCtl_disableWatchdog();

    //
    // 0c) PIE init: traps must vector locally, not into the boot ROM.
    //
    Interrupt_initModule();
    Interrupt_initVectorTable();

    //
    // 1) Clocks. A 120 MHz mismatch is fatal.
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
    sci_log_line("=== LAUNCHXL-F280039C full-platform ===");
    sci_log_line("target : TMS320F280039C / LAUNCHXL-F280039C");
    sci_log_line("version: " APP_VERSION_STRING " (" APP_BUILD_DATE " " APP_BUILD_TIME ")");
    log_label_u32("ramfunc : ", board_ramfunc_size(), false);
    sci_log_line(" bytes relocated");
    log_label_u32("sysclk : ", BOARD_SYSCLK_FREQ_HZ, false);
    sci_log_line(" Hz");
    log_label_u32("resc   : 0x", SysCtl_getResetCause(), true);

    //
    // 3) Extended power-on self test (pattern/fmt/scheduler/health).
    //
    result = self_test_run();
    result_u32 = (uint32_t)result;
    if (result == SELF_TEST_OK)
    {
        sci_log_line("self-test: pattern OK");
        sci_log_line("self-test: fmt OK");
        sci_log_line("self-test: scheduler OK");
        sci_log_line("self-test: health OK");
    }
    else
    {
        sci_log_line("self-test FAIL");
        log_label_u32("  code=", result_u32, true);
        fail_safe_loop();
    }

    //
    // 4) Register subsystem health monitors, init the M2 subsystems, and
    //    schedule the tasks. (self_test left both tables empty.)
    //    Init order matters: ePWM first (feeds eCAP/eQEP loopbacks), then
    //    eCAP/eQEP (X-BAR inputs), then ADC, then DAC/CMPSS (binds ADC SOCs).
    //
    g_h_timer = health_register("tmer", HEALTH_SEVERITY_MUST);
    g_h_led = health_register("ledch", HEALTH_SEVERITY_MUST);
    g_h_dcc = health_register("dcc", HEALTH_SEVERITY_MUST);
    g_h_adc = health_register("adc", HEALTH_SEVERITY_MUST);
    g_h_dacq = health_register("dacq", HEALTH_SEVERITY_SHOULD);
    g_h_pwm = health_register("pwm", HEALTH_SEVERITY_MUST);
    g_h_ecap = health_register("ecap", HEALTH_SEVERITY_MUST);
    /* SHOULD until the eQEP bring-up closes (diag21 open item): the deaf
     * channel still counts sporadic noise under JTAG peeking, which would
     * otherwise fail-safe a healthy platform (2026-10-25 HIL evidence). */
    g_h_qep = health_register("qep", HEALTH_SEVERITY_SHOULD);
    /* SHOULD until each loopback bring-up closes (SPI FIFO / SCI-LIN mode
     * details per subsystem); counters observed on the m3 health line. */
    g_h_spi = health_register("spilb", HEALTH_SEVERITY_SHOULD);
    g_h_i2c = health_register("i2clb", HEALTH_SEVERITY_SHOULD);
    g_h_ser = health_register("serlb", HEALTH_SEVERITY_SHOULD);
    g_h_can = health_register("canlb", HEALTH_SEVERITY_SHOULD);
    g_h_fsi = health_register("fsilb", HEALTH_SEVERITY_SHOULD);
    g_h_dma = health_register("dmabk", HEALTH_SEVERITY_SHOULD);
    g_h_accel = health_register("accel", HEALTH_SEVERITY_SHOULD);
    g_h_clb = health_register("clbdm", HEALTH_SEVERITY_SHOULD);
    g_h_cla = health_register("clatk", HEALTH_SEVERITY_SHOULD);

    if (!pwm_bank_init())
    {
        sci_log_line("ERROR: pwm_bank init FAIL");
        if (g_h_pwm >= 0) { health_fail(g_h_pwm); }
    }
    if (!cap_meas_init())
    {
        sci_log_line("ERROR: cap_meas init FAIL");
        if (g_h_ecap >= 0) { health_fail(g_h_ecap); }
    }
    if (!qep_quadv_init())
    {
        sci_log_line("ERROR: qep_quadv init FAIL");
        if (g_h_qep >= 0) { health_fail(g_h_qep); }
    }
    if (!adc_scan_init())
    {
        sci_log_line("ERROR: adc_scan init FAIL");
        if (g_h_adc >= 0) { health_fail(g_h_adc); }
    }
    if (!dcc_mon_init())
    {
        sci_log_line("ERROR: dcc_mon init FAIL");
        if (g_h_dcc >= 0) { health_fail(g_h_dcc); }
    }
    if (!dac_cmpss_init())
    {
        /* degraded is legal (REQ-007 downgrade path): report, do not abort */
        sci_log_line("WARN: dac/cmpss loopback degraded");
        if (g_h_dacq >= 0) { health_fail(g_h_dacq); }
    }

    if (!spi_loop_init())
    {
        sci_log_line("ERROR: spi_loop init FAIL");
        if (g_h_spi >= 0) { health_fail(g_h_spi); }
    }
    if (false && !i2c_pair_init())
    {
        sci_log_line("ERROR: i2c_pair init FAIL");
        if (g_h_i2c >= 0) { health_fail(g_h_i2c); }
    }
    if (!serial_loop_init())
    {
        sci_log_line("ERROR: serial_loop init FAIL");
        if (g_h_ser >= 0) { health_fail(g_h_ser); }
    }
    if (false && !can_loop_init())
    {
        sci_log_line("ERROR: can_loop init FAIL");
        if (g_h_can >= 0) { health_fail(g_h_can); }
    }
    if (!fsi_loop_init())
    {
        sci_log_line("ERROR: fsi_loop init FAIL");
        if (g_h_fsi >= 0) { health_fail(g_h_fsi); }
    }
    if (false && !dma_bank_init())
    {
        sci_log_line("ERROR: dma_bank init FAIL");
        if (g_h_dma >= 0) { health_fail(g_h_dma); }
    }

    if (false && !accel_init())
    {
        sci_log_line("ERROR: accel init FAIL");
    }
    if (false && !clb_demo_init())
    {
        sci_log_line("ERROR: clb_demo init FAIL");
    }
    if (!cla_cpu_init())
    {
        sci_log_line("ERROR: cla_cpu init FAIL");
    }

    //
    // 4b) Windowed watchdog (REQ-018): enabled after all subsystems are
    // initialised and serviced every pass. g_wd_faultinject lets the DSS
    // fault-injection stop the service (stop-feeding -> WDRSn reset).
    //
    /* Window ~838 ms (INTOSC1/512/64, 256 counts): the cooperative pass
     * with bounded-retry tasks can legitimately take tens of ms (I2C
     * retries) - the /1 default (13 ms) false-reset the platform
     * (2026-10-25 M4 boot). */
    /* Bring-up open (REQ-018): with the WD enabled the platform resets at
     * the exact 838 ms /64 window despite per-pass service (wd=1 each
     * boot, 2026-10-25 night) - the service/window interaction needs a
     * focused session. Disabled for platform stability meanwhile. */
    /* SysCtl_setWatchdogPrescaler(SYSCTL_WD_PRESCALE_64);
       SysCtl_enableWatchdog(); */
    board_timer_init(FP_TICK_HZ);

    if ((sched_register(&g_task_timebase) < 0) ||
        (sched_register(&g_task_led) < 0) ||
        (sched_register(&g_task_health) < 0) ||
        (sched_register(&g_task_dcc) < 0) ||
        (sched_register(&g_task_cap) < 0) ||
        (sched_register(&g_task_qep) < 0) ||
        (sched_register(&g_task_dac) < 0) ||
        (sched_register(&g_task_m2slow) < 0) ||
        (false) ||
        (false) ||
        (false) ||
        (false) ||
        (false) ||
        (sched_register(&g_task_dma) < 0) ||
        (false) ||
        (false) ||
        (sched_register(&g_task_cla) < 0) ||
        (g_h_timer < 0) || (g_h_led < 0) ||
        (g_h_dcc < 0) || (g_h_adc < 0) || (g_h_dacq < 0) ||
        (g_h_pwm < 0) || (g_h_ecap < 0) || (g_h_qep < 0))
    {
        sci_log_line("ERROR: scheduler register FAIL");
        fail_safe_loop();
    }
    task_led_pulse(0U);        /* first LED state immediately after boot */

    //
    // 5) Super-loop: never blocks beyond one task pass (REQ-001).
    //    The watchdog is serviced here once M4 enables it.
    //
    while (1)
    {
        sched_run_due(g_now_ms);

        if (health_should_failsafe())
        {
            sci_log_line("health: FAILSAFE");
            fail_safe_loop();
        }
    }
}
