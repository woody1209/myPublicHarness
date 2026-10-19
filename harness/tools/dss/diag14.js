// diag14.js - diff the entire GPIO register block BEFORE and AFTER running
// the firmware.
//
// Why: the HIL run reported H3 PASS (g_self_test_code == 0) but that global is
// statically initialised to 0, so it is also 0 when main() never reaches
// self_test_run(). Meanwhile no GPIO control register shows our pin mux /
// direction writes. This script snapshots every GPIO control and data
// register immediately after loadProgram (firmware has not run yet) and again
// after the firmware has been running, and prints only the registers that
// changed - which localises exactly what the firmware writes, and where.

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

function pad(v, n) {
    var s = v.toString(16).toUpperCase();
    while (s.length < n) { s = "0" + s; }
    return s;
}

var BLOCKS = [
    { base: 0x7C00, len: 0x00A0, name: "GPIOCTRL 0x7C00" },
    { base: 0x7F00, len: 0x0020, name: "GPIODATA 0x7F00" },
    { base: 0x7F80, len: 0x0010, name: "GPIODATAREAD 0x7F80" }
];

var before = {};
var after  = {};

function snapshot(store) {
    for (var b = 0; b < BLOCKS.length; b++) {
        var base = BLOCKS[b].base, len = BLOCKS[b].len;
        for (var a = base; a < base + len; a += 2) {
            try { store[a] = Number(debugSession.memory.readWord(0, a)); }
            catch (ex) { store[a] = null; }
        }
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
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }

try { debugSession.memory.loadProgram(outFile); log("loaded"); }
catch (ex) { log("loadProgram FAILED: " + ex); java.lang.System.exit(1); }

// ---- BEFORE: firmware has not executed yet -------------------------------
try { debugSession.target.halt(); } catch (ex) { }
sleep(100);
log("=== BEFORE (firmware not yet run) ===");
snapshot(before);
for (var b = 0; b < BLOCKS.length; b++) {
    var base = BLOCKS[b].base, len = BLOCKS[b].len;
    log("  -- " + BLOCKS[b].name + " --");
    for (var a = base; a < base + len; a += 2) {
        if (before[a]) {
            log("    0x" + pad(a, 5) + " = 0x" + pad(before[a], 4));
        }
    }
}

// ---- run the firmware ----------------------------------------------------
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }
sleep(2500);
try { debugSession.target.halt(); } catch (ex) { }
sleep(200);

log("=== AFTER (firmware ran ~2.5 s) ===");
snapshot(after);

log("=== CHANGED REGISTERS ===");
var any = false;
for (var b = 0; b < BLOCKS.length; b++) {
    var base = BLOCKS[b].base, len = BLOCKS[b].len;
    for (var a = base; a < base + len; a += 2) {
        if (before[a] !== after[a]) {
            log("    0x" + pad(a, 5) + " : 0x" + pad(before[a], 4) +
                " -> 0x" + pad(after[a], 4));
            any = true;
        }
    }
}
if (!any) { log("    (nothing changed - firmware writes NO GPIO register)"); }

log("=== firmware state ===");
try { log("    PC = 0x" + pad(Number(debugSession.expression.evaluate("PC")), 6)); }
catch (ex) { log("    PC read failed"); }
try { log("    g_self_test_code = " + Number(debugSession.expression.evaluate("g_self_test_code"))); }
catch (ex) { log("    g_self_test_code read failed"); }
try { log("    GPADAT(0x7F00) = 0x" + pad(Number(debugSession.memory.readWord(0, 0x7F00)), 4)); }
catch (ex) { }

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG14 DONE");
