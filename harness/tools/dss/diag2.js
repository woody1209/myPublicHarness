// diag2.js - map out which memory regions are readable, test restart() path,
// and read the reset-cause register (RESC @ 0x5D404 per CPU_SYS_REGS layout).

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }

function tryRead(tag, addr) {
    try {
        var v = Number(debugSession.memory.readWord(0, addr));
        log("  READ " + tag + " (0x" + addr.toString(16) + ") = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) {
        log("  READ " + tag + " (0x" + addr.toString(16) + ") FAILED");
        return null;
    }
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");

debugServer.setConfig(ccxml);
debugSession = debugServer.openSession("*", "*");
debugSession.target.connect();

log("=== region readability (freshly connected, halted) ===");
tryRead("M0 SARAM", 0x00000);
tryRead("M1 SARAM", 0x00400);
tryRead("LS4 RAM", 0x0A000);
tryRead("GS0 RAM", 0x0C000);
tryRead("CPUSYS", 0x5D300);
tryRead("PIE", 0x0CE0);
tryRead("CPUTIMER", 0x0C04);
tryRead("FLASH", 0x80000);
tryRead("GPIOCTRL", 0x7C00);
tryRead("GPIODATA", 0x7F00);
tryRead("GPIODATAREAD", 0x7F80);

log("=== RESC (reset cause) @0x5D404 / RESCCLR @0x5D400 ===");
tryRead("RESCCLR", 0x5D400);
tryRead("RESC lo", 0x5D404);
tryRead("RESC hi", 0x5D405);

log("=== loadProgram + restart() path (no chip reset) ===");
debugSession.memory.loadProgram(outFile);
debugSession.target.restart();
log("  after restart isHalted=" + debugSession.target.isHalted());
var pc0 = debugSession.expression.evaluate("PC");
log("  PC after restart = 0x" + Number(pc0).toString(16).toUpperCase());
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
var pc1 = debugSession.expression.evaluate("PC");
log("  PC after 1.5s run = 0x" + Number(pc1).toString(16).toUpperCase());
try {
    var st = debugSession.expression.evaluate("g_self_test_code");
    log("  g_self_test_code = " + st);
} catch (ex) {
    log("  g_self_test_code read FAILED: " + ex);
}
tryRead("GPIODATA after run", 0x7F00);

log("=== restart again + longer run ===");
debugSession.target.restart();
debugSession.target.runAsynch();
sleep(2500);
debugSession.target.halt();
sleep(100);
var pc2 = debugSession.expression.evaluate("PC");
log("  PC after 2.5s run = 0x" + Number(pc2).toString(16).toUpperCase());
tryRead("GPIODATA after run2", 0x7F00);
tryRead("FLASH after run2", 0x80000);

log("=== real reset + run (boot ROM path) ===");
debugSession.target.reset();
sleep(300);
var pc3 = debugSession.expression.evaluate("PC");
log("  PC after reset = 0x" + Number(pc3).toString(16).toUpperCase());
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
var pc4 = debugSession.expression.evaluate("PC");
log("  PC after reset+1.5s run = 0x" + Number(pc4).toString(16).toUpperCase());
tryRead("RESC after", 0x5D404);

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG2 DONE");
