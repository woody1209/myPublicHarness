// diag13.js - resolve the authoritative GPIO register addresses from the
// device header symbols, then scan the GPIO control + data blocks for the
// registers that actually hold our pin configuration.
//
// Background: driverlib indexes GPIO registers as
//     base[GPIO_O_xxx / 2]   with base declared as (uint32_t *)
// which lands on base + 2*GPIO_O_xxx. The device header also defines the
// GpioCtrlRegs / GpioDataRegs structures at absolute addresses, and those
// symbols are present in the linked image - so we can ask the debugger where
// the registers really are instead of guessing the unit convention.

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
        log("    0x" + pad(addr, 5) + "  " + tag + " READ FAILED");
        return null;
    }
}

function sym(name) {
    try {
        var a = Number(debugSession.symbol.getAddress(name));
        log("    symbol " + name + " = 0x" + pad(a, 6));
        return a;
    } catch (ex) {
        log("    symbol " + name + " : not found");
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
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }

try { debugSession.memory.loadProgram(outFile); log("loaded"); }
catch (ex) { log("loadProgram FAILED: " + ex); java.lang.System.exit(1); }

log("=== device header symbols (authoritative addresses) ===");
sym("GpioCtrlRegs");
sym("GpioDataRegs");
sym("code_start");
sym("main");
sym("led_hw_init");
sym("RamfuncsRunStart");
sym("RamfuncsLoadStart");
sym("RamfuncsLoadSize");
sym("g_self_test_code");

// run the firmware so the chaser and the console are live
try {
    debugSession.expression.evaluate("PC = code_start");
    debugSession.target.runAsynch();
} catch (ex) { log("run failed: " + ex); }
sleep(1500);

function scan(base, len, tag) {
    log("=== scan " + tag + " (0x" + pad(base, 5) + " .. +0x" + pad(len, 3) + ") ===");
    var any = false;
    for (var a = base; a < base + len; a += 2) {
        try {
            var v = Number(debugSession.memory.readWord(0, a));
            if (v !== 0) {
                log("    0x" + pad(a, 5) + " = 0x" + pad(v, 4) + "   (" + v.toString(2) + ")");
                any = true;
            }
        } catch (ex) { }
    }
    if (!any) { log("    (all zero)"); }
}

scan(0x7C00, 0x00A0, "GPIO control block");
scan(0x7F00, 0x0040, "GPIO data block");
scan(0x7F80, 0x0010, "GPIO data read block");

log("=== PC ===");
try { log("    PC = 0x" + pad(Number(debugSession.expression.evaluate("PC")), 6)); }
catch (ex) { log("    PC read failed"); }

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG13 DONE");
