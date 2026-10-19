// diag8.js - board state forensics for the board_clock_init hang
//
// Questions answered on the real board:
//   Q1  What is the current clock/flash/peripheral state right now?
//   Q2  What are the RESET DEFAULTS (target.reset() then read)?
//   Q3  Does the proper boot path (boot ROM -> flash -> main) reach main?
//   Q4  After main, where does the CPU end up? PLL locked? multipliers
//       programmed? DCC status? LED data register?
//
// Usage: dss.bat diag8.js <ccxml> <out-file>

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

// ---- register map (f28003x) -------------------------------------------------
var ADDR = {
    FRDCNTL:      0x5F800,  // FLASH0CTRL_BASE + FLASH_O_FRDCNTL(0)
    CLKSRCCTL1:   0x5D208,  // CLKCFG_BASE + SYSCTL_O_CLKSRCCTL1(0x8)
    SYSPLLCTL1:   0x5D20E,  // CLKCFG_BASE + SYSCTL_O_SYSPLLCTL1(0xE)
    SYSPLLMULT:   0x5D214,  // CLKCFG_BASE + SYSCTL_O_SYSPLLMULT(0x14)
    SYSPLLSTS:    0x5D216,  // CLKCFG_BASE + SYSCTL_O_SYSPLLSTS(0x16)
    SYSCLKDIVSEL: 0x5D222,  // CLKCFG_BASE + SYSCTL_O_SYSCLKDIVSEL(0x22)
    DCC0_STATUS:  0x5E714,  // DCC0_BASE + DCC_O_STATUS(0x14)
    WDCR:         0x7029,   // WD_BASE + 0x29
    RESC:         0x5D404,  // reset cause
    GPADAT:       0x7F00
};

function rd(name) {
    try {
        var v = Number(debugSession.memory.readWord(0, ADDR[name]));
        log("    " + name + " = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("    " + name + " READ FAILED: " + ex);
        return null;
    }
}

function rdPC() {
    try {
        var v = Number(debugSession.expression.evaluate("PC"));
        log("    PC = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("    PC READ FAILED: " + ex);
        return null;
    }
}

function dumpAll(tag) {
    log("  --- state: " + tag + " ---");
    rdPC();
    rd("FRDCNTL"); rd("CLKSRCCTL1"); rd("SYSPLLCTL1"); rd("SYSPLLMULT");
    rd("SYSPLLSTS"); rd("SYSCLKDIVSEL"); rd("DCC0_STATUS");
    rd("WDCR"); rd("RESC"); rd("GPADAT");
}

function haltAndWait() {
    debugSession.target.halt();
    var waited = 0;
    while (waited < 3000) {
        if (debugSession.target.isHalted()) { return true; }
        sleep(10);
        waited += 10;
    }
    return debugSession.target.isHalted();
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
var debugSession = null;

try { debugServer.setConfig(ccxml); }
catch (ex) { log("setConfig failed: " + ex); java.lang.System.exit(1); }

try {
    debugSession = debugServer.openSession("*", "*");
    log("board : " + debugSession.getBoardName());
} catch (ex) { log("openSession failed: " + ex); java.lang.System.exit(1); }

try { debugSession.options.setBoolean("AutoRunToLabelOnRestart", false); }
catch (ex) { log("warn: AutoRunToLabelOnRestart: " + ex); }

try { debugSession.target.connect(); }
catch (ex) { log("connect failed: " + ex); java.lang.System.exit(1); }
log("H1 PASS: JTAG connected");

try { debugSession.expression.evaluate("GEL_MapOff()"); log("GEL_MapOff done"); }
catch (ex) { log("GEL_MapOff failed: " + ex); }

// ---- Q1: current state -------------------------------------------------------
if (!haltAndWait()) { log("warn: would not halt for state dump"); }
dumpAll("CURRENT (as found)");

// ---- Q2: reset defaults ------------------------------------------------------
log("=== target.reset() ===");
try {
    debugSession.target.reset();
    sleep(300);
    haltAndWait();
} catch (ex) { log("reset failed: " + ex); }
dumpAll("AFTER RESET (reset defaults)");

// ---- Q3: proper boot path: reset -> boot ROM -> flash -> main ----------------
log("=== boot path: run to main ===");
try {
    var bpMain = debugSession.breakpoint.add("main");
    log("  bp main id=" + bpMain);
} catch (ex) { log("  bp main add FAILED: " + ex); }
try {
    debugSession.target.reset();
    sleep(200);
    debugSession.target.runAsynch();
    var waited = 0;
    while (waited < 8000) {
        if (debugSession.target.isHalted()) { break; }
        sleep(50);
        waited += 50;
    }
    log("  halted after " + waited + "ms, isHalted=" + debugSession.target.isHalted());
} catch (ex) { log("  run failed: " + ex); }
dumpAll("AT MAIN (after full boot ROM path)");

// ---- Q4: remove breakpoints, run 2.5s, where does it end up? -----------------
log("=== plain run 2.5s ===");
try {
    debugSession.breakpoint.removeAll();
    log("  breakpoints removed");
} catch (ex) { log("  removeAll FAILED: " + ex); }
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
    sleep(2500);
    if (!haltAndWait()) { log("  warn: would not halt after plain run"); }
} catch (ex) { log("  plain run failed: " + ex); }
dumpAll("AFTER 2.5s RUN FROM code_start");

// one more short run to see if PC moves at all
try {
    debugSession.target.runAsynch();
    sleep(400);
    haltAndWait();
} catch (ex) { log("  second run failed: " + ex); }
dumpAll("AFTER +0.4s MORE");

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG8 DONE");
