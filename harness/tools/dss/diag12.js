// diag12.js - dump the whole GPIO data + control register block twice
//
// Symptom after the ramfunc fix: g_self_test_code == 0 (firmware runs), the
// chaser cadence is visible, but GPADAT(0x7F00) never shows GPIO20/GPIO22
// driven - instead bits 0..13 toggle between 0x3FFF and 0x0000.
//
// This script dumps every GPIO data register (0x7F00..0x7F20), the read-back
// block (0x7F80..0x7F90) and the control registers that decide mux /
// direction (0x7C00..0x7C30), twice, ~250 ms apart, so the toggling bits can
// be localised to an exact register.

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

function pad(v, n) {
    var s = v.toString(16).toUpperCase();
    while (s.length < n) { s = "0" + s; }
    return s;
}

function rd16(tag, addr) {
    try {
        var v = Number(debugSession.memory.readWord(0, addr));
        log("    0x" + pad(addr, 5) + "  " + tag + " = 0x" + pad(v, 4));
        return v;
    } catch (ex) {
        log("    0x" + pad(addr, 5) + "  " + tag + " READ FAILED: " + ex);
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
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }

try { debugSession.memory.loadProgram(outFile); log("loaded"); }
catch (ex) { log("loadProgram FAILED: " + ex); java.lang.System.exit(1); }

// run the firmware for a while so the chaser is up and the console is live
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }
sleep(1500);

function dump(tag) {
    log("=== " + tag + " ===");
    log("  -- GPIODATA (0x7F00, write block) --");
    rd16("GPADAT    ", 0x7F00);
    rd16("GPASET    ", 0x7F02);
    rd16("GPACLEAR  ", 0x7F04);
    rd16("GPATOGGLE ", 0x7F06);
    rd16("GPBDAT    ", 0x7F08);
    rd16("GPBSET    ", 0x7F0A);
    rd16("GPBCLEAR  ", 0x7F0C);
    rd16("GPBTOGGLE ", 0x7F0E);
    log("  -- GPIODATAREAD (0x7F80, read block) --");
    rd16("GPADAT_R  ", 0x7F80);
    rd16("GPBDAT_R  ", 0x7F82);
    log("  -- GPIOCTRL (0x7C00) --");
    rd16("GPAMUX1   ", 0x7C06);
    rd16("GPAMUX2   ", 0x7C08);
    rd16("GPADIR    ", 0x7C0A);
    rd16("GPAPUD    ", 0x7C0C);
    rd16("GPAGMUX1  ", 0x7C20);
    rd16("GPAGMUX2  ", 0x7C22);
    log("");
}

dump("SAMPLE 1");
sleep(250);
dump("SAMPLE 2");

log("=== decode ===");
log("  LED4 = GPIO20 -> GPAMUX2 bits [5:4], GPADIR bit 20");
log("  LED5 = GPIO22 -> GPAMUX2 bits [9:8], GPADIR bit 22");

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG12 DONE");
