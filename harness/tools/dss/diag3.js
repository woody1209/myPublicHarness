// diag3.js - (a) test GEL_MapOff for peripheral reads
//             (b) sample PC at increasing run durations to find the reset point

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
function tryEval(tag, expr) {
    try {
        var v = debugSession.expression.evaluate(expr);
        log("  EVAL " + tag + " [" + expr + "] = " + v);
        return v;
    } catch (ex) {
        log("  EVAL " + tag + " [" + expr + "] FAILED: " + ex);
        return null;
    }
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");

debugServer.setConfig(ccxml);
debugSession = debugServer.openSession("*", "*");
debugSession.target.connect();

log("=== (a) peripheral reads BEFORE GEL_MapOff ===");
tryRead("GPIODATAREAD", 0x7F80);
tryRead("CPUTIMER", 0x0C04);

log("=== (a) call GEL_MapOff() ===");
tryEval("GEL_MapOff", "GEL_MapOff()");
tryRead("GPIODATAREAD", 0x7F80);
tryRead("CPUTIMER", 0x0C04);
tryRead("GPIODATA", 0x7F00);
tryRead("GPIOCTRL", 0x7C00);

log("=== (b) PC sampling: restart path ===");
debugSession.memory.loadProgram(outFile);
var durs = [30, 100, 300, 800, 1500];
for (var i = 0; i < durs.length; i++) {
    debugSession.target.restart();
    debugSession.target.runAsynch();
    sleep(durs[i]);
    debugSession.target.halt();
    sleep(80);
    var pc = debugSession.expression.evaluate("PC");
    log("  restart + run " + durs[i] + "ms -> PC = 0x" + Number(pc).toString(16).toUpperCase());
}

log("=== (b) PC sampling: real reset path ===");
for (var j = 0; j < durs.length; j++) {
    debugSession.target.reset();
    debugSession.target.runAsynch();
    sleep(durs[j]);
    debugSession.target.halt();
    sleep(80);
    var pc2 = debugSession.expression.evaluate("PC");
    log("  reset + run " + durs[j] + "ms -> PC = 0x" + Number(pc2).toString(16).toUpperCase());
}

log("=== (c) final state: LEDs + self-test after reset+2s ===");
debugSession.target.reset();
debugSession.target.runAsynch();
sleep(2000);
debugSession.target.halt();
sleep(100);
tryEval("g_self_test_code", "g_self_test_code");
tryRead("GPIODATA", 0x7F00);
tryRead("GPIODATAREAD", 0x7F80);
tryRead("PIE IFR@0x0CEF", 0x0CEF);

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG3 DONE");
