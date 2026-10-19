// diag15.js - DEFINITIVE trace: where do GPIO_writePin's SET/CLEAR writes land?
//
// Strategy: breakpoint at symbol GPIO_writePin. Between two consecutive hits
// exactly one writePin() call ran to completion. Snapshot the whole GPIO data
// block at every hit and diff consecutive snapshots -> the words that changed
// are the registers the driverlib ACTUALLY wrote. ACC at the hit holds the
// pin number (C28x first-arg convention). Also dumps the flash const table
// led_hw_channels (word 0x84432) to prove the pin numbers in flash, the
// GPADAT high word (0x7F01, where LED4 bit20 / LED5 bit22 live), the 32-bit
// GPADAT_R read register (0x7F80), and the key GPIO control registers.
//
// Usage: dss.bat diag15.js <ccxml> <out-file>

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function pad(v, n) {
    var s = v.toString(16).toUpperCase();
    while (s.length < n) { s = "0" + s; }
    return s;
}

var DATA_BASE = 0x7F00, DATA_LEN = 0x20;      // GPIODATA  block
var READ_BASE = 0x7F80, READ_LEN = 0x10;      // GPIODATAREAD block
var HITS = 10;

function snapshot() {
    var s = {};
    var a;
    for (a = DATA_BASE; a < DATA_BASE + DATA_LEN; a += 2) {
        try { s[a] = Number(debugSession.memory.readWord(0, a)); }
        catch (ex) { s[a] = null; }
    }
    for (a = READ_BASE; a < READ_BASE + READ_LEN; a += 2) {
        try { s[a] = Number(debugSession.memory.readWord(0, a)); }
        catch (ex) { s[a] = null; }
    }
    return s;
}

function rd(addr) {
    try { return Number(debugSession.memory.readWord(0, addr)); }
    catch (ex) { return null; }
}

function rdReg(name) {
    try { return Number(debugSession.expression.evaluate(name)); }
    catch (ex) { log("    reg " + name + " read failed"); return null; }
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
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }
try { debugSession.memory.loadProgram(outFile); log("loaded"); }
catch (ex) { log("loadProgram FAILED: " + ex); java.lang.System.exit(1); }

// ---- flash const table (word addressing) ------------------------------------
log("=== flash const: led_hw_channels @ 0x84432 (expect 20,0x0808, 22,0x080C) ===");
for (var a = 0x84432; a < 0x8443A; a += 2) {
    log("    0x" + pad(a, 5) + " = 0x" + pad(rd(a), 4));
}

// ---- breakpoint trace -------------------------------------------------------
try { debugSession.breakpoint.add("GPIO_writePin"); log("bp @ GPIO_writePin ok"); }
catch (ex) { log("bp add FAILED: " + ex); java.lang.System.exit(1); }

try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }

var prev = null;
for (var hit = 0; hit < HITS; hit++) {
    var waited = 0;
    while (waited < 6000) {
        if (debugSession.target.isHalted()) { break; }
        sleep(20); waited += 20;
    }
    if (!debugSession.target.isHalted()) { log("hit[" + hit + "]: no halt"); break; }
    var acc = rdReg("ACC");
    var pc = rdReg("PC");
    log("hit[" + hit + "] PC=0x" + pad(pc, 6) + "  ACC(pin)=0x" + pad(acc, 4) +
        "  (" + acc + ")");
    var s = snapshot();
    if (prev !== null) {
        log("  --- changed by the writePin call that just completed ---");
        var any = false;
        for (var k in s) {
            if (s[k] !== prev[k]) {
                log("    0x" + pad(Number(k), 5) + " : 0x" + pad(prev[k], 4) +
                    " -> 0x" + pad(s[k], 4));
                any = true;
            }
        }
        if (!any) { log("    (nothing changed!)"); }
    }
    prev = s;
    debugSession.target.runAsynch();
}

// ---- final register state ----------------------------------------------------
try { debugSession.target.halt(); } catch (ex) { }
sleep(200);
log("=== final GPIO state (halted) ===");
log("    GPADAT lo  0x7F00 = 0x" + pad(rd(0x7F00), 4) + "   (GPIO0-15)");
log("    GPADAT hi  0x7F01 = 0x" + pad(rd(0x7F01), 4) + "   (GPIO16-31: bit4=LED4/GPIO20, bit6=LED5/GPIO22)");
log("    GPADAT_R   0x7F80 = 0x" + pad(rd(0x7F80), 4) + " 0x" + pad(rd(0x7F81), 4) + "  (32-bit read reg)");
log("    GPAMUX2    0x7C08 = 0x" + pad(rd(0x7C08), 4) + " 0x" + pad(rd(0x7C09), 4));
log("    GPADIR     0x7C0A = 0x" + pad(rd(0x7C0A), 4) + " 0x" + pad(rd(0x7C0B), 4));
log("    GPAPUD     0x7C0C = 0x" + pad(rd(0x7C0C), 4) + " 0x" + pad(rd(0x7C0D), 4));
log("    GPAGMUX2   0x7C22 = 0x" + pad(rd(0x7C22), 4) + " 0x" + pad(rd(0x7C23), 4));
log("    GPAMUX1    0x7C06 = 0x" + pad(rd(0x7C06), 4) + " 0x" + pad(rd(0x7C07), 4) + "  (GPIO0-15 mux)");
log("    GPADIR alt 0x7C14 = 0x" + pad(rd(0x7C14), 4) + "  (device-header DIR candidate)");

try { debugSession.breakpoint.removeAll(); } catch (ex) { }
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
    sleep(300);
    debugSession.target.disconnect();
    debugServer.stop();
} catch (ex) { log("teardown warning: " + ex); }
log("DIAG15 DONE");
