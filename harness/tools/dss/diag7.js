// diag7.js - breakpoint tracing: does execution reach main from code_start?

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function tryRead(tag, addr) {
    try { var v = Number(debugSession.memory.readWord(0, addr)); log("  READ " + tag + " = 0x" + v.toString(16).toUpperCase()); return v; }
    catch (ex) { log("  READ " + tag + " FAILED"); return null; }
}
function tryEval(tag, expr) {
    try { var v = debugSession.expression.evaluate(expr); log("  EVAL " + tag + " [" + expr + "] = " + v); return v; }
    catch (ex) { log("  EVAL " + tag + " [" + expr + "] FAILED: " + ex); return null; }
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
debugSession.target.connect();
debugSession.options.setBoolean("AutoRunToLabelOnRestart", false);
tryEval("GEL_MapOff", "GEL_MapOff()");

log("=== loadProgram ===");
debugSession.memory.loadProgram(outFile);
tryEval("PC after load", "PC");

log("=== breakpoints ===");
try {
    var bpMain = debugSession.breakpoint.add("main");
    log("  bp main id=" + bpMain);
    var bpSci = debugSession.breakpoint.add("sci_log_init");
    log("  bp sci_log_init id=" + bpSci);
    var bpClock = debugSession.breakpoint.add("board_clock_init");
    log("  bp board_clock_init id=" + bpClock);
    var bpSelfTest = debugSession.breakpoint.add("self_test_run");
    log("  bp self_test_run id=" + bpSelfTest);
} catch (ex) {
    log("  breakpoint add FAILED: " + ex);
}

log("=== run from code_start, 2s ===");
tryEval("PC = code_start", "PC = code_start");
debugSession.target.runAsynch();
sleep(2000);
debugSession.target.halt();
sleep(100);
tryEval("PC after run", "PC");
tryRead("GPADAT", 0x7F00);

log("=== single step x20 to see movement ===");
// no step API on target; instead: continue running in 100ms slices
for (var i = 0; i < 3; i++) {
    debugSession.target.runAsynch();
    sleep(300);
    debugSession.target.halt();
    sleep(80);
    tryEval("PC slice" + i, "PC");
}

log("=== remove breakpoints, plain run 2s ===");
try { debugSession.breakpoint.removeAll(); } catch (ex) { log("removeAll: " + ex); }
tryEval("PC = code_start", "PC = code_start");
debugSession.target.runAsynch();
sleep(2000);
debugSession.target.halt();
sleep(100);
tryEval("PC plain run", "PC");

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG7 DONE");
