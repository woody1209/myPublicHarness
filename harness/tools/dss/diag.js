// diag.js - pinpoint why memory reads fail after run/halt cycles
//
// Stages: each stage prints PC + reads of flash/data/peripheral addresses so we
// can see exactly when access breaks.

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
        log("  READ " + tag + " (0x" + addr.toString(16) + ") FAILED: " + ex);
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
log("board: " + debugSession.getBoardName());

debugSession.target.connect();
log("STAGE 1: freshly connected (no load)");
log("  isHalted=" + debugSession.target.isHalted());
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryRead("ctrl@7C00", 0x7C00);
tryEval("PC", "PC");

log("STAGE 2: loadProgram");
debugSession.memory.loadProgram(outFile);
log("  isHalted=" + debugSession.target.isHalted());
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryEval("PC", "PC");

log("STAGE 3: reset (no run)");
debugSession.target.reset();
sleep(300);
log("  isHalted=" + debugSession.target.isHalted());
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryEval("PC", "PC");
tryEval("sym g_self_test_code", "g_self_test_code");

log("STAGE 4: run 500ms then halt");
debugSession.target.runAsynch();
sleep(500);
debugSession.target.halt();
log("  isHalted=" + debugSession.target.isHalted());
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryEval("PC", "PC");
tryEval("sym g_self_test_code", "g_self_test_code");

log("STAGE 5: waitForHalt + read");
try {
    debugSession.target.waitForHalt();
    log("  waitForHalt returned, isHalted=" + debugSession.target.isHalted());
} catch (ex) {
    log("  waitForHalt threw: " + ex);
}
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryEval("PC", "PC");

log("STAGE 6: restart (PC to entry, no chip reset) + read");
try {
    debugSession.target.restart();
    log("  restart ok, isHalted=" + debugSession.target.isHalted());
    tryRead("periph@7F00", 0x7F00);
    tryEval("PC", "PC");
} catch (ex) {
    log("  restart threw: " + ex);
}

log("STAGE 7: run 1s, halt via waitForHalt pattern");
debugSession.target.runAsynch();
sleep(1000);
debugSession.target.halt();
sleep(200);
log("  isHalted=" + debugSession.target.isHalted());
tryRead("flash@80000", 0x80000);
tryRead("periph@7F00", 0x7F00);
tryRead("data@a80c", 0xa80c);
tryEval("PC", "PC");

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG DONE");
