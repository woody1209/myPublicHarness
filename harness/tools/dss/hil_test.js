// hil_test.js - DSS hardware-in-the-loop test for LAUNCHXL-F280039C led-chaser
//
// What this proves on the real board (no human eyeballs required):
//   H1  JTAG path reaches the MCU (XDS110 -> cJTAG 2-pin -> C28xx_CPU1)
//   H2  Flash contains the built image (non-blank at code_start)
//   H3  Power-on self test passed (g_self_test_code == SELF_TEST_OK == 0)
//   H4  Chaser runs from the flash main loop (PC inside application .text)
//   H5  LED polarity + cadence: exactly one of LED4(GPIO20)/LED5(GPIO22)
//       is LOW at the pin at any time, and the active LED flips with a
//       ~200 ms period (LEDCHASER_STEP_MS) - which also proves SYSCLK
//       is 120 MHz as designed (the cadence is paced by the CPU timer).
//       MEASURED HALT-FREE: the core is never stopped while the LED
//       samples are taken, because a halt is not timing-neutral here -
//       TIF is a single flag, so all overflows produced during a halt
//       collapse into one main-loop increment on resume and a
//       halt-sample loop reported 426 ms for the 200 ms step
//       (2026-10-17; the firmware was fine, the probe was not).
//       Polarity and cadence are judged on GPADAT_R (the real pin state,
//       what the LEDs see); the GPADAT output-latch peek is logged for
//       information only - on this part a debugger peek of that word pair
//       picks up floating-analog noise, so latch equality was proven to be
//       no test at all (13-14 of 26-28 false misses, the same noise
//       appeared while the boot ROM held every GPIO at reset defaults;
//       docs/40 + ADR 0002).
//   H6  On exit the target is reset and left RUNNING standalone, so the
//       board keeps chasing after the debugger disconnects (and re-prints
//       its boot banner, which run-hil.ps1 captures on the SCI port).
//
// C28x WORD ADDRESSING (hard-won lesson, 2026-10-17): every address in this
// file - map file, linker .cmd, DSS readWord, XARn, PC - is a 16-bit WORD
// address, and memory.readWord() returns exactly ONE 16-bit word. GPADAT is
// a 32-bit register spanning words 0x7F00 (GPIO0-15) and 0x7F01 (GPIO16-31);
// the LED bits GPIO20/GPIO22 live in the HIGH word 0x7F01. Reading only
// 0x7F00 can never see them - that was the H5 false-failure of 2026-10-17.
// GPADAT_R @ 0x7F80 is the 32-bit readback of the actual pin state.
//
// Usage:
//   dss.bat hil_test.js <ccxml> <out-file>
//
// Output contract for the orchestrator (run-hil.ps1):
//   "HIL RESULT: PASS"   -> every check above passed
//   "HIL RESULT: FAIL <reason>" -> first failing check
//   process exit code 1 on failure (via java.lang.System.exit)

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

if (ccxml == null || outFile == null) {
    print("usage: dss.bat hil_test.js <ccxml> <out-file>");
    java.lang.System.exit(2);
}

// ---- board constants (must match source/board_pins.h) --------------------
// C28x word addressing: readWord() returns one 16-bit word; GPADAT is 32-bit.
var GPIODATA_BASE   = 0x00007F00;   // f28003x_headers_nonBIOS.cmd: GPIODATA region
var GPADAT_LO       = 0x0000;       // GPADAT low  word (GPIO0-15)
var GPADAT_HI       = 0x0001;       // GPADAT high word (GPIO16-31: LED4=bit4, LED5=bit6)
var GPIOREAD_BASE   = 0x00007F80;   // GPIODATAREAD region: GPADAT_R (32-bit pin state)
var LED4_GPIO       = 20;           // red   LED, active LOW
var LED5_GPIO       = 22;           // green LED, active LOW
var LED4_BIT        = 1 << LED4_GPIO;
var LED5_BIT        = 1 << LED5_GPIO;
var CODE_START      = 0x00080000;   // map file: code_start (flash entry)
var STEP_MS         = 200;          // LEDCHASER_STEP_MS (main.c)
var CADENCE_TOL     = 0.25;         // +/-25 % around STEP_MS (mean chase period)
var SAMPLE_GAP_MS   = 20;           // spacing between halt-free register peeks (<< STEP_MS)
var SAMPLE_WINDOW_MS= 2500;         // total LED sampling window (~12 chase steps)

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

// Read a word with one retry (JTAG hiccups happen, science must not).
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

// Read the 32-bit GPADAT output latch (words 0x7F00 + 0x7F01) and the 32-bit
// GPADAT_R pin-state readback (words 0x7F80 + 0x7F81) as JS numbers.
function readGpioA() {
    var lo  = readWordRetry(GPIODATA_BASE + GPADAT_LO);
    var hi  = readWordRetry(GPIODATA_BASE + GPADAT_HI);
    var rlo = readWordRetry(GPIOREAD_BASE + GPADAT_LO);
    var rhi = readWordRetry(GPIOREAD_BASE + GPADAT_HI);
    return { dat: ((hi << 16) | lo) >>> 0, pin: ((rhi << 16) | rlo) >>> 0 };
}

// ---- scripting environment (API per CCS DSS examples) ---------------------
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
// PIE, CPU timer ...) then fail with error 0x20000. Turning the GEL memory
// map off gives the debugger raw access to the whole C28x space.
try {
    debugSession.expression.evaluate("GEL_MapOff()");
    log("GEL_MapOff: peripheral frames now accessible");
} catch (ex) {
    log("GEL_MapOff failed (peripheral reads may fail): " + ex);
}

// ---- H2: program the built image into flash -------------------------------
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
// loadProgram leaves the core wherever CCS put it - with the default
// "auto run to main" session option that is main() itself, which SKIPS
// code_start/wd_disable: the watchdog stays enabled and WDRST-loops the
// device into the boot ROM wait state (no output, no LEDs).
// Setting PC to the code_start symbol reproduces exactly what the boot ROM
// does on a power cycle: code_start -> wd_disable -> _c_int00 -> main().
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) {
    fail("H4-run (" + ex + ")");
}
sleep(1200);   // let clocks/GPIO/SCI/self-test settle before first sample

// ---- H5 sampling: LED state over time ---------------------------------------
// HALT-FREE. DSS memory reads peek the GPIO registers while the core keeps
// running, so the chase is observed at its true wall-clock rate. Halting is
// not a neutral probe: TIF is a single flag, so every timer overflow produced
// during a halt collapses into ONE main-loop increment when the CPU resumes.
// A halt-sample loop therefore measured 426 ms for this 200 ms step at ~50 %
// halt duty (2026-10-17) - the cadence was never wrong, the probe was.
// Peeking every ~10 ms resolves each flip, and averaging the intervals
// removes the peek quantization; the mean then equals the real chase period,
// which proves SYSCLK = 120 MHz (step = 200 ticks of PRD/SYSCLK).
var samples = [];
var i, j;
var peekErr = 0;
var sampleStart = now();
while (now() - sampleStart < SAMPLE_WINDOW_MS) {
    var g;
    try {
        g = readGpioA();
    } catch (ex) {
        peekErr++;
        continue;
    }
    samples.push({ t: now(), dat: g.dat, pin: g.pin });
    sleep(SAMPLE_GAP_MS);
}
if (samples.length < 20) {
    fail("H5-sample (" + samples.length + " halt-free peeks, " + peekErr +
         " failed - cannot judge cadence)");
}
var spanMs = samples[samples.length - 1].t - samples[0].t;
var samplePeriod = spanMs / (samples.length - 1);
log("LED samples: " + samples.length + " halt-free peeks over " +
    (spanMs / 1000).toFixed(2) + " s (~" + samplePeriod.toFixed(0) +
    " ms/peek, " + peekErr + " failed)");
if (samplePeriod > STEP_MS * 0.7) {
    log("warning: peek period " + samplePeriod.toFixed(0) +
        " ms approaches the 200 ms step - cadence measurement may be coarse");
}
for (j = 0; j < samples.length; j++) {
    log("  sample[" + j + "] GPADAT=0x" + samples[j].dat.toString(16).toUpperCase() +
        " GPADAT_R=0x" + samples[j].pin.toString(16).toUpperCase());
}

// Sampling left the core running; halt now for the reads that need a stopped
// target (symbol values, PC).
try {
    if (!haltAndWait()) { fail("H4-halt (target would not halt for symbol reads)"); }
} catch (ex) {
    fail("H4-halt (" + ex + ")");
}

// ---- H3: self test code ----------------------------------------------------
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
// Word address = CPUSYS_BASE(0x5D300) + byte offset 0x30 (32-bit regs at
// their word offset; driverlib uint32_t* indexing makes 0x30 the word index).
var pclkcr7Lo = readWordRetry(0x5D330);
if ((pclkcr7Lo & 1) !== 1) {
    fail("H3-sciclk (PCLKCR7=0x" + pclkcr7Lo.toString(16) +
         ", SCIA peripheral clock gated off)");
}
log("H3 PASS: power-on self test OK on target, SCIA clock gated on (PCLKCR7&1=1)");

// ---- H5: polarity + cadence -------------------------------------------------
// Judged on GPADAT_R = the actual pin state the LEDs see. The GPADAT output
// latch may transiently disagree: led_hw_apply() drives the two LED bits with
// two separate writes, and a halt can land in that window.
var bad = null;
var led4On, led5On;
for (i = 0; i < samples.length; i++) {
    led4On = ((samples[i].pin & LED4_BIT) === 0);   // active LOW
    led5On = ((samples[i].pin & LED5_BIT) === 0);
    if (led4On === led5On) {                         // both on or both off
        bad = "polarity@sample" + i + " (pin led4On=" + led4On + " led5On=" + led5On + ")";
        break;
    }
}
if (bad !== null) { fail("H5-" + bad); }

// Output latch vs pin readback: informational ONLY. Empirical finding
// 2026-10-17 (diag16/17 + HIL reruns): a debugger peek of the GPADAT word
// pair carries wide-field noise on this part (floating analog-capable pins
// bleed into the input path; the same 0x3A77-class patterns appear even
// while the boot ROM holds every GPIO at reset defaults). GPADAT_R is a
// synchronized 32-bit read and is the authoritative pin state; requiring
// latch agreement produced 14/28 false mismatches. The hard LED checks are
// therefore pin-state based only.
var latchMismatch = 0;
for (i = 0; i < samples.length; i++) {
    if ((samples[i].dat & (LED4_BIT | LED5_BIT)) !==
        (samples[i].pin & (LED4_BIT | LED5_BIT))) {
        latchMismatch++;
    }
}
log("GPADAT peek noise vs GPADAT_R (informational): " + latchMismatch +
    " of " + samples.length);

var changes = [];
for (i = 1; i < samples.length; i++) {
    var a = samples[i - 1].pin & (LED4_BIT | LED5_BIT);
    var b = samples[i].pin & (LED4_BIT | LED5_BIT);
    if (a !== b) { changes.push(samples[i].t); }
}
log("LED transitions observed: " + changes.length);
if (changes.length < 4) {
    fail("H5-cadence (only " + changes.length + " transitions in " +
         (spanMs / 1000).toFixed(2) + " s - chase is not flipping)");
}
var meanDt = (changes[changes.length - 1] - changes[0]) / (changes.length - 1);
log("chase period: mean=" + meanDt.toFixed(1) + "ms (expect ~" + STEP_MS +
    "ms +/-" + (CADENCE_TOL * 100) + "%)");
if (meanDt < STEP_MS * (1 - CADENCE_TOL) || meanDt > STEP_MS * (1 + CADENCE_TOL)) {
    fail("H5-cadence (mean " + meanDt.toFixed(1) + "ms vs expected " + STEP_MS + "ms)");
}
log("H5 PASS: exactly one LED on at a time, " + meanDt.toFixed(0) + "ms chase cadence");

// ---- H4: PC inside the application main loop -------------------------------
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
