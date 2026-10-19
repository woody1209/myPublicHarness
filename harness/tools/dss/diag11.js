// diag11.js - catch the ITRAP in the act and read the faulting address
//
// The firmware traps with an illegal instruction within ~20 ms of starting
// from code_start. With the PIE disabled the vector comes from the boot ROM
// (CPU1BROM_itrapISR @ 0x3FEEC9), which stores the faulting PC in
// CPU1BROM_itrapAddress (RAM 0x1E) before resetting the device.
//
// This script reproduces the trap and reads the faulting address BEFORE any
// reset wipes it.

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

function rd16(tag, addr) {
    try {
        var v = Number(debugSession.memory.readWord(0, addr));
        log("  " + tag + " (0x" + addr.toString(16).toUpperCase() + ") = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("  " + tag + " READ FAILED: " + ex);
        return null;
    }
}

function rdPC() {
    try {
        var v = Number(debugSession.expression.evaluate("PC"));
        log("  PC = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("  PC READ FAILED: " + ex);
        return null;
    }
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
catch (ex) { log("warn: " + ex); }
try { debugSession.target.connect(); }
catch (ex) { log("connect failed: " + ex); java.lang.System.exit(1); }
log("H1 PASS: connected");
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { log("GEL_MapOff: " + ex); }

try { debugSession.memory.loadProgram(outFile); log("loaded"); }
catch (ex) { log("loadProgram FAILED: " + ex); java.lang.System.exit(1); }

// run from code_start, catch the trap fast
log("=== run from code_start (expect ITRAP within ~20ms) ===");
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }

for (var i = 0; i < 10; i++) {
    sleep(10);
    debugSession.target.halt();
    sleep(50);
    var pc = rdPC();
    if (pc !== null && pc >= 0x3FEEC9 && pc <= 0x3FEEFA) {
        log("  *** ITRAP HANDLER REACHED ***");
        break;
    }
    // keep running toward the trap
    try { debugSession.target.runAsynch(); } catch (ex) { }
}
debugSession.target.halt();
sleep(100);
rdPC();

log("=== boot ROM trap diagnostics (same session, pre-reset) ===");
rd16("bootStatus",      0x0002);
rd16("bootStatus hi",   0x0004);
rd16("nmiStatus",       0x0008);
rd16("nmiStatus hi",    0x000A);
var lo = rd16("itrapAddress lo", 0x001E);
var hi = rd16("itrapAddress hi", 0x0020);
if (lo !== null && hi !== null) {
    var fault = (hi * 65536) + lo;
    log("  *** FAULTING PC (itrapAddress) = 0x" + fault.toString(16).toUpperCase() + " ***");
}

log("=== clock state at trap ===");
rd16("FRDCNTL",    0x5F800);
rd16("CLKSRCCTL1", 0x5D208);
rd16("XTALCR",     0x5D232);
rd16("SYSPLLCTL1", 0x5D20E);
rd16("SYSPLLMULT", 0x5D214);
rd16("SYSPLLSTS",  0x5D216);
rd16("SYSCLKDIVSEL", 0x5D222);
rd16("WDCR",       0x7029);
rd16("GPADAT",     0x7F00);

// dump flash around the faulting address to identify the instruction
if (lo !== null && hi !== null) {
    var fault = (hi * 65536) + lo;
    var base = fault & 0xFFF0;
    log("=== flash words around 0x" + fault.toString(16).toUpperCase() + " ===");
    for (var a = base; a < base + 0x20; a += 2) {
        try {
            var v = Number(debugSession.memory.readWord(0, a));
            log("    0x" + a.toString(16).toUpperCase() + " : 0x" + v.toString(16).toUpperCase().padStart(4, "0") +
                (a === fault ? "   <== FAULT" : ""));
        } catch (ex) { log("    read failed at 0x" + a.toString(16)); break; }
    }
}

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG11 DONE");
