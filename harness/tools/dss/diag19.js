// diag19.js - halt-free timing ground truth.
// Premise (hard-won 2026-10-17): every debugger HALT perturbs this firmware's
// timing (timer ICEM, two-write GPIO windows, peek noise on GPADAT). So we
// measure with the core RUNNING: DSS memory reads peek peripheral registers
// without halting the CPU. Host wall clock is the reference.
//
// Outputs:
//   1) static truth: PRD / TCR / TPR / PCLKCR0 / GPIO DIR+MUX (sanity)
//   2) timer clock rate: live TIM peeks over ~1 s of run time
//   3) LED flip rate: live GPADAT_R peeks at ~10 ms cadence for 3 s
// Usage: dss.bat diag19.js <ccxml>

var ccxml = arguments[0];

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function hex(n) { return '0x' + (n < 0 ? 'ERR' : (n >>> 0).toString(16).toUpperCase()); }
function rd(addr) {
    for (var a = 0; a < 3; a++) {
        try { return Number(debugSession.memory.readWord(0, addr)); } catch (ex) { }
    }
    return -1;
}
function w32(lo) {
    var l = rd(lo), h = rd(lo + 1);
    return (l < 0 || h < 0) ? -1 : ((h << 16) | l) >>> 0;
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
try { debugSession.options.setBoolean("AutoRunToLabelOnRestart", false); } catch (ex) { }
debugSession.target.connect();
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }
try { debugSession.expression.evaluate("PC = 0x80000"); } catch (ex) { log('PC set failed: ' + ex); }
debugSession.target.runAsynch();
log('running from 0x80000 (no symbols loaded - halt-free mode)');
sleep(1500);   // let boot + SCI banner pass

// ---- 1) static truth (live peeks) ----
log('PRD      = ' + hex(w32(0x0C02)) + ' (expect 30000=0x7530)');
log('TCR      = ' + hex(rd(0x0C04)) + ' TPR=' + hex(rd(0x0C06)) + '+' + hex(rd(0x0C07)));
log('PCLKCR0  = ' + hex(w32(0x5D322)) + ' (bit3 CPUTIMER0 clk)');
log('PCLKCR7  = ' + hex(w32(0x5D330)) + ' (bit0 SCIA clk)');
log('SCIA CTL1= ' + hex(rd(0x7201)) + ' HBAUD=' + hex(rd(0x7202)) + ' LBAUD=' + hex(rd(0x7203)));
log('GPIO MUX1/2 = ' + hex(w32(0x7C06)) + '/' + hex(w32(0x7C08)) +
    ' DIR=' + hex(w32(0x7C0A)) + ' (want mux20=0 mux22=0 dir20=1 dir22=1)');
log('CTL1 expect 0x000B (RXENA|TXENA|SWRESET); HBAUD/LBAUD 15 for 115200@30MHz');

// ---- 2) timer clock rate via live TIM peeks ----
// TIM counts down inside one period [0..PRD]; per second it decrements
// f_clk, so (PRD - TIM2) - (PRD - TIM1) mod PRD-ish is messy across wraps.
// Cleaner: sum of *decrements* between consecutive peeks, handling wrap:
// dec(i) = (TIM(i-1) - TIM(i) + PRD+1) mod (PRD+1)  [counts down to 0 then reload]
var prd = w32(0x0C02);
if (prd <= 0) { log('bad PRD, abort rate test'); java.lang.System.exit(1); }
var t0 = rd(0x0C00);
var hStart = new Date().getTime();
var decrements = 0, prev = t0;
var peekCount = 0, errCount = 0;
var tEnd;
while (true) {
    sleep(50);
    var now = rd(0x0C00);
    if (now < 0) { errCount++; continue; }
    peekCount++;
    var d = (prev - now) % (prd + 1);
    if (d < 0) { d += (prd + 1); }
    decrements += d;
    prev = now;
    tEnd = new Date().getTime();
    if (tEnd - hStart >= 1000) { break; }
}
var dtMs = tEnd - hStart;
var fTimer = decrements / (dtMs / 1000);
log('TIM live-peeks: ' + peekCount + ' ok / ' + errCount + ' err over ' + dtMs + ' ms');
log('timer clock (measured) = ' + Math.round(fTimer) + ' Hz  ' +
    '(SYSCLK if /1; 30MHz => SYSCLK/4 at 120MHz)');
log('decrements=' + decrements + ' PRD=' + prd + ' => tick rate = ' +
    (fTimer / (prd + 1)).toFixed(1) + ' Hz (want 1000)');

// ---- 3) LED flip rate via live GPADAT_R peeks (10 ms spacing, 3 s) ----
var LED_MASK = (1 << 4) | (1 << 6);   // bits in high word: GPIO20, GPIO22
var prevBits = -1, flips = 0, flipTimes = [];
var ledStart = new Date().getTime(), ledPrev = ledStart;
var onBits = [];
while (new Date().getTime() - ledStart < 3000) {
    var hi = rd(0x7F81);
    var t = new Date().getTime();
    if (hi < 0) { continue; }
    var bits = hi & LED_MASK;
    if (prevBits >= 0 && bits !== prevBits) {
        flips++;
        flipTimes.push(t - ledPrev);
    }
    if (prevBits < 0) { flipTimes.push(t - ledPrev); }
    prevBits = bits;
    ledPrev = t;
    sleep(10);
}
var ledSpan = ledPrev - ledStart;
log('LED peeks done: ' + flips + ' flips over ' + ledSpan + ' ms');
var mean = flips > 0 ? ledSpan / flips : -1;
log('mean flip interval = ' + (mean < 0 ? 'n/a' : mean.toFixed(1)) + ' ms (want 200 if step=200ms, 2 LEDs)');
log('intervals: ' + flipTimes.slice(0, 24).join(','));

// ---- 4) cross-check: logical self-test var + final state ----
log('g_self_test_code(RAM 0xA80C) = ' + hex(rd(0xA80C)));

try { debugSession.target.disconnect(); } catch (ex) { log('disconnect: ' + ex); }
debugServer.stop();
log('DIAG19 DONE - board left RUNNING');
java.lang.System.exit(0);
