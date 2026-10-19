// diag17.js - ground-truth probe after diag16 (2026-10-17 late night).
//
// diag16 facts: PC sits in boot ROM (0x3FDxxx), GPIO regs at reset defaults,
// SCIA CTL1=0x0000 -> the board is NOT running our firmware right now, even
// though flash was programmed at the 21:38 HIL run (H2 verified non-blank and
// the debugger-driven code_start run made the LEDs chase). Open question:
// does a REAL cold boot (reset -> boot ROM -> flash) reach the application?
//
// Hypotheses under test:
//   H-A  standalone flash boot fails / WDRST-loops because the firmware never
//        enables the SCIA peripheral clock (PCLKCR7 bit0; TI InitSysCtrl sets
//        it, our board_clock_init does not).
//   H-B  the 21:38 SCI silence is that same missing clock gate (SCI register
//        writes are no-ops without it) - firmware ran, LEDs chased, TX dead.
//   H-C  the 21:38 GPADAT/GPADAT_R "mismatch" was our own mis-decoded GPIO
//        control map (32-bit regs, stride 2 words: MUX1@+6 MUX2@+7 DIR@+8
//        PUD@+9 INV@+C ODR@+D AMSEL@+E GMUX1@+14 GMUX2@+15).
//
// Read-only apart from PC/reset control. No flash writes.
// Usage: dss.bat diag17.js <ccxml>

var ccxml = arguments[0];

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function hex(n) { return '0x' + (n < 0 ? 'ERR' : (n >>> 0).toString(16).toUpperCase()); }

function readW(addr) {
    for (var a = 0; a < 3; a++) {
        try { return Number(debugSession.memory.readWord(0, addr)); }
        catch (ex) { sleep(30); }
    }
    return -1;
}
function w32(lo) {
    var l = readW(lo), h = readW(lo + 1);
    if (l < 0 || h < 0) { return -1; }
    return ((h << 16) | l) >>> 0;
}

function haltAndWait() {
    try { debugSession.target.halt(); } catch (ex) { }
    for (var waited = 0; waited < 3000; waited += 20) {
        if (debugSession.target.isHalted()) { return true; }
        sleep(20);
    }
    return debugSession.target.isHalted();
}
function pc() { try { return Number(debugSession.expression.evaluate("PC")); } catch (ex) { return -1; } }

// GPIOCTRL base 0x7C00, 32-bit regs, stride 2 words (struct GPIO_CTRL_REGS)
var GC = 0x7C00;
var SCIA = 0x7200;
var CPUSYS = 0x5D300;

function dump(tag) {
    log('--- ' + tag + ' ---');
    var p = pc();
    var where = '???';
    if (p >= 0x3F0000) { where = 'BOOT ROM'; }
    else if (p >= 0x80000 && p <= 0x88000) { where = 'APP FLASH'; }
    else if (p >= 0x8000 && p <= 0xA800) { where = 'RAM (ramfunc/vars)'; }
    log('  PC=' + hex(p) + '  [' + where + ']');
    log('  RESC      =' + hex(w32(CPUSYS + 0x80)) +
        '  PCLKCR7   =' + hex(w32(CPUSYS + 0x18)) + ' (bit0=SCIA clock)');
    log('  PCLKCR0   =' + hex(w32(CPUSYS + 0x11)) + ' (bit3=CPUTIMER0)');
    log('  GPADAT    =' + hex(w32(0x7F00)) + '  GPADAT_R=' + hex(w32(0x7F80)));
    log('  GPAMUX1/2 =' + hex(w32(GC + 6)) + '/' + hex(w32(GC + 8)) +
        '  GPAGMUX2=' + hex(w32(GC + 30)) +
        '  GPADIR=' + hex(w32(GC + 10)));
    log('  GPAPUD    =' + hex(w32(GC + 12)) + '  GPAODR=' + hex(w32(GC + 17)));
    log('  mux20=' + ((w32(GC + 8) >>> 8) & 3) + ' mux22=' + ((w32(GC + 8) >>> 12) & 3) +
        ' mux28=' + ((w32(GC + 8) >>> 24) & 3) + ' mux29=' + ((w32(GC + 8) >>> 26) & 3) +
        ' (28/29: 1=SCIA)');
    log('  SCIA CCR=' + hex(readW(SCIA)) + ' CTL1=' + hex(readW(SCIA + 1)) +
        ' CTL2=' + hex(readW(SCIA + 4)) + ' RXST=' + hex(readW(SCIA + 5)));
    log('  SCIA HBAUD=' + hex(readW(SCIA + 2)) + ' LBAUD=' + hex(readW(SCIA + 3)) +
        ' FFTX=' + hex(readW(SCIA + 0xA)) + ' FFRX=' + hex(readW(SCIA + 0xB)));
    log('  g_self_test_code(RAM 0xA80C)=' + hex(readW(0xA80C)));
    var c1 = readW(SCIA + 1);
    if (c1 >= 0) {
        log('  CTL1: RXENA=' + (c1 & 1) + ' TXENA=' + ((c1 >>> 1) & 1) +
            ' SWRESET=' + ((c1 >>> 3) & 1) + ' UARTEN expected bits0,1,3=0xB');
    }
    log('');
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
try { debugSession.options.setBoolean("AutoRunToLabelOnRestart", false); } catch (ex) { }
debugSession.target.connect();
log('connected: ' + debugSession.getBoardName());
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { log('GEL_MapOff failed: ' + ex); }

dump('T0 (halted at attach)');

// ---- real cold boot: reset, let the boot ROM do its flash-boot job ----
log('=== target.reset() + run, watch PC/register evolution ===');
try { debugSession.target.reset(); } catch (ex) { log('reset() failed: ' + ex); }
debugSession.target.runAsynch();
var marks = [50, 200, 500, 1000, 2000, 4000];
var tPrev = 0;
for (var i = 0; i < marks.length; i++) {
    sleep(marks[i] - tPrev); tPrev = marks[i];
    if (!haltAndWait()) { log('halt failed at ' + marks[i] + 'ms'); continue; }
    dump('after reset + ' + marks[i] + ' ms');
    debugSession.target.runAsynch();
}

// ---- A/B test: manually gate the SCIA clock and see if CTL1 writes stick ----
// (firmware sets CTL1 only if it ever reaches sci_log_init; this is a
// debugger-side experiment AFTER any boot: write CTL1 with clock OFF, then ON)
if (!haltAndWait()) { log('cannot halt for A/B'); }
log('=== A/B: SCIA write with PCLKCR7 cleared vs set ===');
var p7 = w32(CPUSYS + 0x18);
log('  PCLKCR7 now = ' + hex(p7));
try {
    debugSession.memory.writeWord(0, SCIA + 1, 0x000B);      // RXENA|TXENA|SWRESET
    sleep(10);
    log('  wrote CTL1=0x000B blind -> readback CTL1=' + hex(readW(SCIA + 1)));
    debugSession.memory.writeWord(0, CPUSYS + 0x18, 1);      // PCLKCR7 low word: SCIA on
    debugSession.memory.writeWord(0, CPUSYS + 0x19, 0);
    debugSession.memory.writeWord(0, SCIA + 1, 0x000B);
    sleep(10);
    log('  after PCLKCR7|=1  -> CTL1=' + hex(readW(SCIA + 1)));
} catch (ex) { log('  A/B write failed (needs EALLOW / write protect): ' + ex); }

// leave the board alone: resume whatever it was doing
try {
    debugSession.target.runAsynch();
    sleep(200);
    debugSession.target.disconnect();
} catch (ex2) { log('teardown warning: ' + ex2); }
debugServer.stop();
log('DIAG17 DONE');
java.lang.System.exit(0);
