// diag9.js - fine-grained PC sampling to locate the exact hang instruction
//
// Loads the image (symbols present), starts from code_start like the HIL test
// does, and samples PC every ~20 ms. The histogram of sampled PCs shows the
// exact loop the CPU is stuck in (or proves it is running normally).
//
// Usage: dss.bat diag9.js <ccxml> <out-file>

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

var ADDR = {
    FRDCNTL:      0x5F800,
    CLKSRCCTL1:   0x5D208,
    XTALCR:       0x5D232,  // CLKCFG_BASE + SYSCTL_O_XTALCR(0x32)
    SYSPLLCTL1:   0x5D20E,
    SYSPLLMULT:   0x5D214,
    SYSPLLSTS:    0x5D216,
    SYSCLKDIVSEL: 0x5D222,
    MCDCR:        0x5D22E,  // CLKCFG_BASE + SYSCTL_O_MCDCR(0x2E)
    DCC0_STATUS:  0x5E714,
    WDCR:         0x7029,
    RESC:         0x5D404,
    GPADAT:       0x7F00
};

function rd(name) {
    try {
        var v = Number(debugSession.memory.readWord(0, ADDR[name]));
        log("    " + name + " = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("    " + name + " READ FAILED");
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
    rd("FRDCNTL"); rd("CLKSRCCTL1"); rd("XTALCR"); rd("SYSPLLCTL1");
    rd("SYSPLLMULT"); rd("SYSPLLSTS"); rd("SYSCLKDIVSEL"); rd("MCDCR");
    rd("DCC0_STATUS"); rd("WDCR"); rd("RESC"); rd("GPADAT");
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

// ---- as-found state (before touching anything) -------------------------------
if (!haltAndWait()) { log("warn: would not halt"); }
dumpAll("AS FOUND (standalone state after last teardown)");

// ---- load image (symbols) -----------------------------------------------------
log("=== loadProgram ===");
try {
    debugSession.memory.loadProgram(outFile);
    log("  loaded");
} catch (ex) { log("  loadProgram FAILED: " + ex); }
dumpAll("AFTER LOAD");

// ---- run from code_start, sample PC every ~20 ms ------------------------------
log("=== run from code_start, 60 PC samples @20ms ===");
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }

var samples = [];
for (var i = 0; i < 60; i++) {
    sleep(20);
    try {
        haltAndWait();
        var pc = Number(debugSession.expression.evaluate("PC"));
        samples.push(pc);
        log("  sample[" + i + "] PC = 0x" + pc.toString(16).toUpperCase());
        debugSession.target.runAsynch();
    } catch (ex) {
        log("  sample[" + i + "] FAILED: " + ex);
        break;
    }
}
haltAndWait();

// histogram
var hist = {};
for (i = 0; i < samples.length; i++) {
    var k = "0x" + samples[i].toString(16).toUpperCase();
    hist[k] = (hist[k] || 0) + 1;
}
log("  --- PC histogram (top) ---");
var keys = [];
for (var k2 in hist) { if (hist.hasOwnProperty(k2)) { keys.push(k2); } }
keys.sort(function (a, b) { return hist[b] - hist[a]; });
for (i = 0; i < keys.length && i < 12; i++) {
    log("    " + keys[i] + " : " + hist[keys[i]] + " samples");
}

dumpAll("AFTER SAMPLING");

// ---- symbol lookup helper ------------------------------------------------------
var syms = ["code_start", "wd_disable", "_c_int00", "main", "board_clock_init",
            "SysCtl_setClock", "SysCtl_getClock", "sci_log_init", "led_hw_init",
            "self_test_run", "SysCtl_disableWatchdog", "SysCtl_delay"];
for (i = 0; i < syms.length; i++) {
    try {
        var v = Number(debugSession.expression.evaluate(syms[i]));
        log("    SYM " + syms[i] + " = 0x" + v.toString(16).toUpperCase());
    } catch (ex) {
        log("    SYM " + syms[i] + " not found");
    }
}

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG9 DONE");
