// diag10.js - read the boot ROM's trap diagnostics from RAM
//
// The C28x core fetches trap/NMI vectors from the boot ROM when the PIE is
// disabled (our firmware never enables it). The ROM handlers record:
//   RAM 0x02 : CPU1BROM_bootStatus  (boot status flags)
//   RAM 0x08 : CPU1BROM_nmiStatus   (NMI status)
//   RAM 0x1E : CPU1BROM_itrapAddress (32-bit faulting PC on ITRAP)
// This script reads them plus the current PC.

var ccxml = (arguments.length > 0) ? arguments[0] : null;

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
try { debugSession.target.connect(); }
catch (ex) { log("connect failed: " + ex); java.lang.System.exit(1); }
log("connected");

try { debugSession.target.halt(); sleep(200); } catch (ex) { log("halt: " + ex); }
log("isHalted = " + debugSession.target.isHalted());

try {
    var pc = Number(debugSession.expression.evaluate("PC"));
    log("  PC = 0x" + pc.toString(16).toUpperCase());
} catch (ex) { log("  PC READ FAILED: " + ex); }

// boot ROM diagnostics in M0 RAM
rd16("bootStatus",      0x0002);
rd16("bootStatus hi",   0x0004);
rd16("nmiStatus",       0x0008);
rd16("nmiStatus hi",    0x000A);
rd16("itrapAddress lo", 0x001E);
rd16("itrapAddress hi", 0x0020);

// dump the first 0x40 bytes of M0 for context
log("  --- M0 RAM dump 0x00-0x3F ---");
for (var a = 0; a < 0x40; a += 2) {
    try {
        var v = Number(debugSession.memory.readWord(0, a));
        log("    0x" + a.toString(16).toUpperCase().padStart(4, "0") + " : 0x" + v.toString(16).toUpperCase().padStart(4, "0"));
    } catch (ex) { break; }
}

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG10 DONE");
