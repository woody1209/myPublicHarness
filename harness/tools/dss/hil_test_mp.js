// hil_test_mp.js - DSS hardware-in-the-loop test for LAUNCHXL-F280039C
// motor-pwm (specs/0003-motor-pwm-skeleton).
//
// What this proves on the real board (no human eyeballs required):
//   H1  JTAG path reaches the MCU (XDS110 -> cJTAG 2-pin -> C28xx_CPU1)
//   H2  Flash contains the built image (non-blank at code_start)
//   H3  Power-on self test passed (g_self_test_code == SELF_TEST_OK == 0)
//   H4  Modulation runs from the flash main loop (PC inside application
//       .text; CMPA keeps changing at the 10 Hz update rate)
//   H5  ePWM/TZ/CMPSS chain, in three stages:
//       5a  Register readback: TBPRD=6000 / DBRED=DBFED=120 /
//           DBCTL&0xF=0x0B / TZSEL.DCBEVT1 on ALL THREE modules; CMPSS DAC
//           parked at 4095; no spurious TZ latch during a normal run.
//       5b  FORCED over-limit experiment: read the live A2 code, drop the
//           DAC 512 codes below it -> comparator trips (COMPSTS=1) ->
//           DCBEVT1 one-shot latches on all three modules (TZFLG bit5) ->
//           all six PWM outputs read LOW on GPADAT_R. Restore DAC=4095 ->
//           the firmware dwells 5 s, clears the latch itself, prints
//           "tz: cleared" and resumes modulating (CMPA registers start
//           moving again - a phase at duty=PRD legitimately reads both its
//           pins low for a whole carrier cycle, so an instantaneous pin
//           snapshot is NOT a reliable resume signal).
//           This exercises the whole REQ-004 chain end to end, including
//           the firmware's fault/recovery state machine.
//       5c  CMPA is time-varying (>=4 distinct values across the three
//           modules over ~1.5 s): the SVPWM modulation loop is really
//           running, not parked on the zero vector.
//   H6  On exit the target is reset and left RUNNING standalone, so the
//       board keeps modulating after the debugger disconnects (and
//       re-prints its boot banner, which run-hil.ps1 captures on SCI).
//
// C28x WORD ADDRESSING (hard-won lesson, 2026-10-17): every address in this
// file is a 16-bit WORD address, and memory.readWord() returns exactly ONE
// 16-bit word. Register offsets below were cross-checked against the
// C2000Ware_26_01_00_00 driverlib hw_epwm.h / hw_cmpss.h / hw_adc.h /
// hw_memmap.h (2026-10-25) - NOT counted by hand:
//   EPWM1/2/3   = 0x4000/0x4100/0x4200
//   TBPRD=0x63 is the HIGH word of the 32-bit TBPRD:TBPRDHR pair at 0x62;
//   CMPA value = word 0x6B, the HIGH word of the 32-bit CMPA:CMPAHR pair at
//   0x6A (hw_epwm.h: EPWM_CMPA_CMPA_M = 0xFFFF0000, CMPAHR = low word).
//   Reading 0x6A reads CMPAHR, which is 0 without HRPWM - that was the
//   "CMPA never changes" false FAIL (2026-10-25): driverlib itself writes
//   HWREGH(base + EPWM_O_CMPA + 1) for COMPA (epwm.h:2649), so the live
//   value only ever appears in the high word.
//   EPWM_O_TZSEL=0x80 TZFLG=0x93 TZCLR=0x97
//   CMPSS1      = 0x5C80, COMPSTS=+0x2, DACHVALS=+0x6
//   ADCARESULT0 = 0x0B00;  GPIODATAREAD (GPADAT_R) = 0x7F80
// PWM outputs are GPIO0-5 -> GPADAT_R low word bits 0-5 (mask 0x3F).
//
// Usage:
//   dss.bat hil_test_mp.js <ccxml> <out-file>
//
// Output contract for the orchestrator (run-hil.ps1):
//   "HIL RESULT: PASS"   -> every check above passed
//   "HIL RESULT: FAIL <reason>" -> first failing check
//   process exit code 1 on failure (via java.lang.System.exit)

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

if (ccxml == null || outFile == null) {
    print("usage: dss.bat hil_test_mp.js <ccxml> <out-file>");
    java.lang.System.exit(2);
}

// ---- board constants (must match source/board_pins.h + board_epwm.c) -------
// C28x word addressing: readWord() returns one 16-bit word.
var EPWM_BASES     = [0x00004000, 0x00004100, 0x00004200];  // ePWM1/2/3
var O_TBPRD        = 0x63;   // EPWM_O_TBPRD   (hw_epwm.h; HIGH word of the
                             // 32-bit TBPRD:TBPRDHR pair at 0x62)
var O_CMPA         = 0x6B;   // CMPA VALUE word = HIGH word of the 32-bit
                             // CMPA:CMPAHR pair at 0x6A (hw_epwm.h:
                             // EPWM_CMPA_CMPA_M = 0xFFFF0000). Word 0x6A is
                             // CMPAHR and reads 0 without HRPWM; driverlib
                             // writes COMPA at base+EPWM_O_CMPA+1 = 0x6B
                             // (epwm.h EPWM_setCounterCompareValue).
var O_DBCTL        = 0x0C;   // EPWM_O_DBCTL   (OUT_MODE@0/POLSEL@2/IN_MODE@4)
var O_DBRED        = 0x51;   // EPWM_O_DBRED
var O_DBFED        = 0x53;   // EPWM_O_DBFED
var O_TZSEL        = 0x80;   // EPWM_O_TZSEL
var O_TZFLG        = 0x93;   // EPWM_O_TZFLG
var PWM_PRD        = 6000;   // MOTORPWM_PWM_PRD (10 kHz up-down @120 MHz)
var DB_CYCLES      = 120;    // MOTORPWM_PWM_DEADBAND_CYCLES
var TZSEL_DCBEVT1  = 0x8000; // TZSEL bit15 (EPWM_TZ_SIGNAL_DCBEVT1)
var TZFLG_DCBEVT1  = 0x0020; // TZFLG bit5  (EPWM_TZ_FLAG_DCBEVT1)

var CMPSS1_BASE    = 0x00005C80;              // hw_memmap.h
var ADDR_COMPSTS   = CMPSS1_BASE + 0x02;      // CMPSS_O_COMPSTS (bit0 = hi)
var ADDR_DACHVALS  = CMPSS1_BASE + 0x06;      // CMPSS_O_DACHVALS (SWLOADSEL=0)
var ADDR_ADC_RES0  = 0x00000B00;              // ADCARESULT_BASE + ADC_O_RESULT0
var DAC_MAX        = 4095;

var GPADAT_R_LO    = 0x00007F80;  // GPIODATAREAD base: GPIO0-15 pin state
var GPADAT_R_HI    = 0x00007F81;  // GPIO16-31 pin state (LED4=bit4, LED5=bit6)
var PWM_PIN_MASK   = 0x3F;        // GPIO0-5 = ePWM1/2/3 A/B outputs
var LED4_BIT       = 0x100000;     // GPIO20, active LOW: bit 20 of the
                                    // combined (hi<<16)|lo GPADAT_R value
var LED5_BIT       = 0x400000;     // GPIO22, active LOW: bit 22 likewise
                                    // (0x10/0x40 would test GPIO4/GPIO6 -
                                    // GPIO4 is ePWM3A and toggles at the
                                    // carrier rate: false H5-led4 FAILs)

var CODE_START     = 0x00080000;  // map file: code_start (flash entry)
var SETTLE_MS      = 2500;        // startup: clocks -> probe -> arm -> selftest
var WINDOW_MS      = 6000;        // halt-free sampling window. One peek costs
                                  // ~9 DSS reads x ~25 ms = ~0.25 s, so the
                                  // old 1.5 s window yielded only ~6 samples
                                  // (below the 20-sample bar - H5-sample leg
                                  // failed on count, not on state). 6 s gives
                                  // ~24 samples and ~60 CMPA updates @10 Hz.
var GAP_MS         = 30;          // peek spacing
var MIN_SAMPLES    = 12;          // statistical floor for the running-state legs
var TRIP_TIMEOUT   = 2000;        // comparator response deadline (ms)
var TZ_TIMEOUT     = 2000;        // latch propagation deadline (ms)
var RECOVER_TIMEOUT= 12000;       // firmware dwell (5 s) + margin

function now() { return new Date().getTime(); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function log(s) { java.lang.System.out.println(s); }

function fail(reason) {
    log("HIL RESULT: FAIL " + reason);
    try { debugSession.target.disconnect(); } catch (e2) { }
    try { debugServer.stop(); } catch (e3) { }
    java.lang.System.exit(1);
}

// Halt and WAIT until the debugger confirms the halted state. A read issued
// while the core is still running fails with 0x20000 - poll instead of assuming.
function haltAndWait() {
    debugSession.target.halt();
    var waited = 0;
    while (waited < 2000) {
        if (debugSession.target.isHalted()) { return true; }
        sleep(10);
        waited += 10;
    }
    return debugSession.target.isHalted();
}

// Read a word with retries (JTAG hiccups happen, science must not).
function readWordRetry(addr) {
    var last = null;
    for (var attempt = 0; attempt < 3; attempt++) {
        try {
            return Number(debugSession.memory.readWord(0, addr));
        } catch (ex) {
            last = ex;
            sleep(50);
        }
    }
    throw last;
}

// Write a word with retries. Signature verified against the TI E2E DSS
// examples: writeWord(addressSpace, address, value).
function writeWordRetry(addr, value) {
    var last = null;
    for (var attempt = 0; attempt < 3; attempt++) {
        try {
            debugSession.memory.writeWord(0, addr, value);
            return;
        } catch (ex) {
            last = ex;
            sleep(50);
        }
    }
    throw last;
}

// GPADAT_R pin-state readback: 32-bit, words 0x7F80 (low) + 0x7F81 (high).
function readGpioAPinRead() {
    var lo = readWordRetry(GPADAT_R_LO);
    var hi = readWordRetry(GPADAT_R_HI);
    return ((hi << 16) | lo) >>> 0;
}

// TZFLG.DCBEVT1 (bit5) state of one ePWM module.
function tzDcbevt1(base) {
    return (readWordRetry(base + O_TZFLG) & TZFLG_DCBEVT1) !== 0;
}

// All three modules latched / all three clear.
function tzAllLatched() {
    for (var i = 0; i < 3; i++) { if (!tzDcbevt1(EPWM_BASES[i])) return false; }
    return true;
}
function tzAllClear() {
    for (var i = 0; i < 3; i++) { if (tzDcbevt1(EPWM_BASES[i])) return false; }
    return true;
}

// ---- scripting environment (API per CCS DSS examples) ----------------------
var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
var debugSession = null;

log("ccxml : " + ccxml);
log("image : " + outFile);

try {
    debugServer.setConfig(ccxml);
} catch (ex) {
    log("setConfig failed: " + ex);
    java.lang.System.exit(1);
}

try {
    debugSession = debugServer.openSession("*", "*");
    log("board : " + debugSession.getBoardName());
} catch (ex) {
    log("openSession failed: " + ex);
    java.lang.System.exit(1);
}

// CCS default: on program load, auto-run to symbol "main" (PC=main + breakpoint
// at main). That SKIPS code_start/wd_disable and halts before main's body ever
// executes. Disable it so a load leaves the core where we put it.
try {
    debugSession.options.setBoolean("AutoRunToLabelOnRestart", false);
    log("AutoRunToLabelOnRestart = false (load no longer jumps to main)");
} catch (ex) {
    log("warning: AutoRunToLabelOnRestart could not be cleared: " + ex);
}

try {
    debugSession.target.connect();
} catch (ex) {
    log("target.connect failed: " + ex);
    fail("H1-connect (check USB, JP1/JP2 shunts, cJTAG ccxml)");
}
log("H1 PASS: JTAG connected (cJTAG)");

// The ccxml GEL "Memory Map Initialization" restricts debugger access to the
// regions it mapped (RAM/flash) - peripheral frames (GPIO 0x7F00, CPUSYS,
// PIE, CPU timer, ePWM ...) then fail with error 0x20000. Turning the GEL
// memory map off gives the debugger raw access to the whole C28x space.
try {
    debugSession.expression.evaluate("GEL_MapOff()");
    log("GEL_MapOff: peripheral frames now accessible");
} catch (ex) {
    log("GEL_MapOff failed (peripheral reads may fail): " + ex);
}

// ---- H2: program the built image into flash --------------------------------
try {
    debugSession.memory.loadProgram(outFile);
} catch (ex) {
    log("loadProgram failed: " + ex);
    fail("H2-load");
}
if (!debugSession.symbol.exists("g_self_test_code")) {
    fail("H2-symbols (our image did not load - symbol g_self_test_code missing)");
}
var codeWord = readWordRetry(CODE_START);
if (codeWord === 0xFFFF || codeWord === 0) {
    fail("H2-blank-flash (code_start reads 0x" + codeWord.toString(16) + ")");
}
log("H2 PASS: flash programmed, code_start=0x" + codeWord.toString(16).toUpperCase() +
    ", image symbols present");

// ---- run from the reset vector ---------------------------------------------
// Same rationale as the led-chaser script: PC = code_start reproduces a power
// cycle (code_start -> wd_disable -> _c_int00 -> main), skipping CCS's default
// "auto run to main" which would leave the watchdog armed.
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) {
    fail("H4-run (" + ex + ")");
}
sleep(SETTLE_MS);   // clocks -> SCI -> ePWM verify -> ADC -> CMPSS probe ->
                    // TZ arm -> self-test -> superloop entry

// ---- H5a/5c: halt-free window over the running firmware --------------------
// Halt-free on purpose (2026-10-17 lesson): a halt is not timing-neutral for
// this firmware either - the superloop is paced by CPU-timer overflow flags
// that collapse during a halt, and the 5 s fault dwell would stall. The core
// keeps running while we peek; every value sampled here is therefore the true
// live state, not a frozen one.
var samples = [];
var i;
var peekErr = 0;
var sampleStart = now();
while (now() - sampleStart < WINDOW_MS) {
    try {
        samples.push({
            t: now(),
            cmp: [readWordRetry(EPWM_BASES[0] + O_CMPA),
                  readWordRetry(EPWM_BASES[1] + O_CMPA),
                  readWordRetry(EPWM_BASES[2] + O_CMPA)],
            tz:  [readWordRetry(EPWM_BASES[0] + O_TZFLG),
                  readWordRetry(EPWM_BASES[1] + O_TZFLG),
                  readWordRetry(EPWM_BASES[2] + O_TZFLG)],
            dac: readWordRetry(ADDR_DACHVALS),
            pin: readGpioAPinRead()
        });
    } catch (ex) {
        peekErr++;
    }
    sleep(GAP_MS);
}
if (samples.length < MIN_SAMPLES) {
    fail("H5-sample (" + samples.length + " halt-free peeks, " + peekErr +
         " failed - cannot judge the running state)");
}
log("running-state samples: " + samples.length + " halt-free peeks over " +
    ((samples[samples.length - 1].t - samples[0].t) / 1000).toFixed(2) + " s (" +
    peekErr + " failed)");

// No spurious trip-zone latch during a normal run (REQ-004 negative path):
// the one-shot is armed, so any latch here would have forced the outputs low
// without a real over-current.
for (i = 0; i < samples.length; i++) {
    for (var m = 0; m < 3; m++) {
        if ((samples[i].tz[m] & TZFLG_DCBEVT1) !== 0) {
            fail("H5-spurious-tz (TZFLG bit5 set on ePWM" + (m + 1) +
                 " during a normal run, sample " + i + ")");
        }
    }
}
log("no spurious TZ latch in " + samples.length + " samples");

// CMPSS reference parked at maximum after the startup probe: a floating A2
// must not be able to assert CTRIPH with the chain armed.
for (i = 0; i < samples.length; i++) {
    if (samples[i].dac !== DAC_MAX) {
        fail("H5-dac-park (DACHVALS=" + samples[i].dac + " at sample " + i +
             ", expected " + DAC_MAX + ")");
    }
}
log("CMPSS DAC parked at " + DAC_MAX + " (reference above any floating input)");

// Running indication: LED5 solid ON, LED4 OFF (board_pins.h truth, active LOW).
for (i = 0; i < samples.length; i++) {
    if ((samples[i].pin & LED5_BIT) !== 0) {
        fail("H5-led5 (LED5 not lit at sample " + i +
             " - firmware is not in the running state)");
    }
    if ((samples[i].pin & LED4_BIT) === 0) {
        fail("H5-led4 (LED4 lit at sample " + i +
             " - firmware is in fault/fail-safe, not running)");
    }
}
log("LED5 solid / LED4 off throughout the window (running state confirmed)");

// ---- H3: self test code (halt for the symbol read) --------------------------
try {
    if (!haltAndWait()) { fail("H4-halt (target would not halt for symbol reads)"); }
} catch (ex) {
    fail("H4-halt (" + ex + ")");
}

var stCode = null;
for (i = 0; i < 3 && stCode === null; i++) {
    try { stCode = Number(debugSession.expression.evaluate("g_self_test_code")); }
    catch (ex) { sleep(50); }
}
if (stCode === null) { fail("H3-read (cannot read g_self_test_code)"); }
log("g_self_test_code = " + stCode);
if (stCode !== 0) {
    fail("H3-selftest (code=" + stCode + ", firmware is in fail-safe)");
}
// REQ-005 root-cause guard (2026-10-17): with CPUSYS_PCLKCR7 bit0 clear the
// SCI accepts no register writes at all - banner/heartbeat silently vanish.
var pclkcr7Lo = readWordRetry(0x5D330);
if ((pclkcr7Lo & 1) !== 1) {
    fail("H3-sciclk (PCLKCR7=0x" + pclkcr7Lo.toString(16) +
         ", SCIA peripheral clock gated off)");
}
log("H3 PASS: power-on self test OK on target, SCIA clock gated on (PCLKCR7&1=1)");

// ---- H5a: ePWM register readback (halted: static config, exact values) -----
// Same checks as board_epwm_verify() plus the trip-zone select that
// board_cmpss_arm_tz() installed. The firmware already ran verify() at
// startup (the "pwm: ... OK" console line); this is the independent
// hardware-side confirmation with the debugger as the second opinion.
for (i = 0; i < 3; i++) {
    var base = EPWM_BASES[i];
    var prd = readWordRetry(base + O_TBPRD);
    var dbred = readWordRetry(base + O_DBRED);
    var dbfed = readWordRetry(base + O_DBFED);
    var dbctl = readWordRetry(base + O_DBCTL);
    var tzsel = readWordRetry(base + O_TZSEL);
    log("ePWM" + (i + 1) + ": TBPRD=" + prd + " DBRED=" + dbred +
        " DBFED=" + dbfed + " DBCTL=0x" + dbctl.toString(16).toUpperCase() +
        " TZSEL=0x" + tzsel.toString(16).toUpperCase());
    if (prd !== PWM_PRD) {
        fail("H5-regs (ePWM" + (i + 1) + " TBPRD=" + prd + ", expected " +
             PWM_PRD + ")");
    }
    if (dbred !== DB_CYCLES || dbfed !== DB_CYCLES) {
        fail("H5-regs (ePWM" + (i + 1) + " dead-band DBRED=" + dbred +
             " DBFED=" + dbfed + ", expected " + DB_CYCLES + ")");
    }
    // OUT_MODE[1:0]=3 (RED+FED both active), POLSEL[1:0]=2 (B complements A).
    if ((dbctl & 0x0F) !== 0x0B) {
        fail("H5-regs (ePWM" + (i + 1) + " DBCTL&0xF=0x" +
             (dbctl & 0x0F).toString(16) + ", expected 0xB)");
    }
    if ((tzsel & TZSEL_DCBEVT1) === 0) {
        fail("H5-regs (ePWM" + (i + 1) + " TZSEL bit15 clear - DCBEVT1 trip " +
             "source not enabled)");
    }
}
log("H5a PASS: ePWM1/2/3 TBPRD/DBRED/DBFED/DBCTL/TZSEL all as designed");

// ---- H5c: CMPA is time-varying (modulation loop really running) ------------
var distinct = {};
var distinctCount = 0;
var maxCmp = 0;
for (i = 0; i < samples.length; i++) {
    for (var m2 = 0; m2 < 3; m2++) {
        var v = samples[i].cmp[m2];
        if (v > PWM_PRD) {
            fail("H5-cmpa (ePWM" + (m2 + 1) + " CMPA=" + v + " > PRD=" +
                 PWM_PRD + " at sample " + i + ")");
        }
        if (v > maxCmp) { maxCmp = v; }
        if (!distinct[v]) { distinct[v] = true; distinctCount++; }
    }
}
log("CMPA values seen: " + distinctCount + " distinct (max=" + maxCmp +
    ", PRD=" + PWM_PRD + ")");
if (distinctCount < 4) {
    fail("H5-cmpa (only " + distinctCount +
         " distinct CMPA values over the window - modulation not running?)");
}

// CMPA transition period: the firmware updates duties at 10 Hz, so successive
// updates should land ~100 ms apart. Informational (the superloop period is
// only "about 10 Hz" per REQ-007), but it sizes the update cadence.
var updates = [];
for (i = 1; i < samples.length; i++) {
    if (samples[i].cmp[0] !== samples[i - 1].cmp[0]) {
        updates.push(samples[i].t);
    }
}
if (updates.length >= 2) {
    var meanUpdate = (updates[updates.length - 1] - updates[0]) /
                     (updates.length - 1);
    log("CMPA transition period: mean=" + meanUpdate.toFixed(0) +
        "ms (expect ~100ms at the 10 Hz telemetry rate)");
}

// ---- H5b: forced over-limit experiment --------------------------------------
// Resume the core first: the firmware must run its fault/recovery state
// machine for this stage to mean anything.
try {
    debugSession.target.runAsynch();
} catch (ex) {
    fail("H5-run (" + ex + ")");
}
sleep(200);

var adc = readWordRetry(ADDR_ADC_RES0);
var tripDac = (adc > 512) ? (adc - 512) : 0;
log("A2 live code (ADCARESULT0) = " + adc + " -> forcing trip DAC to " +
    tripDac + " (reference 512 codes below the measured input)");
// 12-bit result: anything above 0x0FFF means the ADC chain is not sampling
// (the startup CMPSS probe already consumed one blocking conversion, so a
// sane value here is guaranteed unless the SOCA trigger died afterwards).
if (adc > 0x0FFF) {
    fail("H5-adc (ADCARESULT0=0x" + adc.toString(16) +
         " - ADC chain not sampling A2)");
}

writeWordRetry(ADDR_DACHVALS, tripDac);

// Stage 1: the comparator must respond (CMPIN1P above the reference).
var tripDeadline = now() + TRIP_TIMEOUT;
var compTripped = false;
while (now() < tripDeadline) {
    if ((readWordRetry(ADDR_COMPSTS) & 0x1) !== 0) { compTripped = true; break; }
    sleep(10);
}
if (!compTripped) {
    writeWordRetry(ADDR_DACHVALS, DAC_MAX);   // leave the board parked safe
    fail("H5-trip-comp (COMPSTS stayed 0 with DAC=" + tripDac +
         " below the measured A2 code " + adc + ")");
}
log("comparator tripped (COMPSTS=1)");

// Stage 2: DCBEVT1 one-shot must latch on ALL THREE modules.
var tzDeadline = now() + TZ_TIMEOUT;
while (now() < tzDeadline) {
    if (tzAllLatched()) { break; }
    sleep(10);
}
if (!tzAllLatched()) {
    writeWordRetry(ADDR_DACHVALS, DAC_MAX);
    fail("H5-trip-tz (DCBEVT1 did not latch on all three ePWM modules)");
}
log("TZ latched on ePWM1/2/3 (TZFLG bit5 set)");

// Stage 3: all six PWM outputs must read LOW on the pin readback.
var allLow = true;
var pinSamples = [];
for (i = 0; i < 5; i++) {
    var pin = readGpioAPinRead();
    pinSamples.push("0x" + pin.toString(16).toUpperCase());
    if ((pin & PWM_PIN_MASK) !== 0) { allLow = false; }
    sleep(50);
}
log("GPADAT_R during latch: " + pinSamples.join(" "));
if (!allLow) {
    writeWordRetry(ADDR_DACHVALS, DAC_MAX);
    fail("H5-trip-pwm (PWM outputs GPIO0-5 not all LOW while TZ latched)");
}
log("six PWM outputs forced LOW (GPIO0-5 all zero on GPADAT_R)");

// Stage 4: restore the reference, then the FIRMWARE must recover by itself:
// dwell 5 s -> clear the latch -> resume modulating. We only watch.
writeWordRetry(ADDR_DACHVALS, DAC_MAX);
log("reference restored to " + DAC_MAX + " - waiting for the firmware to " +
    "clear the latch and resume (dwell 5000 ms by design, " +
    "MOTORPWM_FAULT_RECOVER_MS)");

var recoverDeadline = now() + RECOVER_TIMEOUT;
var recovered = false;
while (now() < recoverDeadline) {
    // The firmware's recovery (tz: cleared on SCI) is the authoritative
    // signal; TZFLG bit5 clearing on all three modules is the hardware
    // confirmation. PWM pin state resumes one tick later (the motor block
    // runs after the fault_active continue) and is checked separately in
    // the next stage - requiring it HERE races the 1-tick resume.
    if (tzAllClear()) { recovered = true; break; }
    sleep(100);
}
if (!recovered) {
    fail("H5-trip-recover (firmware did not clear TZ within " +
         RECOVER_TIMEOUT + " ms)");
}
log("firmware cleared the latch (TZFLG bit5 clear on ePWM1/2/3)");

// Prove modulation RESUMED after the latch clear. Do NOT use an
// instantaneous GPADAT_R read for this: in this centre-aligned up-down design
// (ZRO->high, UP_CMPA->clear, DOWN_CMPA->set) a phase sitting at
// cmpa = PRD = 1000 keeps BOTH its A/B pins low for the entire carrier cycle
// (the trough at TBCTR=PRD plus the dead-band covers the whole cycle), so a
// single pin snapshot can read all-zero while PWM is running correctly - a
// coincidental false FAIL (seen on target 2026-10-25). The SVPWM duties
// sweep all three phases 0..1000 every motor tick (10 Hz), so the ACTUAL
// "outputs no longer forced low and the modulator is driving again" signal is
// the CMPA registers changing. Poll them for up to ~1.5 s and require at
// least one phase to move.
function readCmpa3() {
    return [readWordRetry(EPWM_BASES[0] + O_CMPA),
            readWordRetry(EPWM_BASES[1] + O_CMPA),
            readWordRetry(EPWM_BASES[2] + O_CMPA)];
}
var resumeBase = readCmpa3();
var resumeDeadline = now() + 1500;
var resumed = false;
while (now() < resumeDeadline) {
    sleep(100);
    var cur = readCmpa3();
    for (var k = 0; k < 3; k++) {
        if (cur[k] !== resumeBase[k]) { resumed = true; break; }
    }
    if (resumed) { break; }
}
if (!resumed) {
    fail("H5-trip-resume (CMPA frozen after TZ clear - modulation did not resume; base=[" +
         resumeBase.join(",") + "])");
}
log("PWM modulation resumed (CMPA registers moving again: " +
    resumeBase.join(",") + " -> " + readCmpa3().join(",") + ")");
log("H5b PASS: CMPSS->XBAR->DCBH->DCBEVT1 one-shot chain proven end to end " +
    "(trip, six-output shutdown, autonomous recovery)");
log("H5 PASS");

// ---- H4: PC inside the application main loop --------------------------------
try {
    if (!haltAndWait()) { fail("H4-halt (target would not halt for PC read)"); }
} catch (ex) {
    fail("H4-halt (" + ex + ")");
}
var pc = null;
for (i = 0; i < 3 && pc === null; i++) {
    try { pc = Number(debugSession.expression.evaluate("PC")); }
    catch (ex) { sleep(50); }
}
if (pc === null) { fail("H4-pc (cannot read PC)"); }
log("PC = 0x" + pc.toString(16).toUpperCase());
if (pc < CODE_START || pc > (CODE_START + 0x8000)) {
    fail("H4-pc (0x" + pc.toString(16) + " outside application flash)");
}
log("H4 PASS: CPU halted inside application code (not c_int00 / fail-safe)");

// ---- H6: leave the board running standalone ---------------------------------
// NOTE: a plain target.reset() while the debugger is connected parks the boot
// ROM in its "wait for debugger" loop - the board would NOT run standalone.
// Re-pointing PC at code_start and detaching is the equivalent of a power
// cycle through the same instruction stream (code_start -> wd_disable ->
// _c_int00 -> main). The re-print of the boot banner that follows is what
// run-hil.ps1's serial capture listens for.
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
    sleep(300);
    debugSession.target.disconnect();
    debugServer.stop();
} catch (ex) {
    log("teardown warning: " + ex);
}
log("H6 PASS: target left running standalone from code_start");

log("HIL RESULT: PASS");
java.lang.System.exit(0);
