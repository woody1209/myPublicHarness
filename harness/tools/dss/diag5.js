// diag5.js - verify PC assignment + trace execution after load

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function tryRead(tag, addr) {
    try {
        var v = Number(debugSession.memory.readWord(0, addr));
        log("  READ " + tag + " (0x" + addr.toString(16) + ") = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) { log("  READ " + tag + " FAILED"); return null; }
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

log("option before: AutoRunToLabelOnRestart = " + debugSession.options.getBoolean("AutoRunToLabelOnRestart"));
debugSession.options.setBoolean("AutoRunToLabelOnRestart", false);
log("option after : AutoRunToLabelOnRestart = " + debugSession.options.getBoolean("AutoRunToLabelOnRestart"));
tryEval("GEL_MapOff", "GEL_MapOff()");

log("=== loadProgram ===");
debugSession.memory.loadProgram(outFile);
tryEval("PC after load", "PC");

log("=== set PC = code_start ===");
tryEval("PC = code_start", "PC = code_start");
tryEval("PC after set", "PC");

log("=== run 1.5s ===");
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
tryEval("PC after run", "PC");
tryEval("g_self_test_code", "g_self_test_code");
tryRead("GPADAT", 0x7F00);

log("=== set PC = main, run 1.5s ===");
tryEval("PC = main", "PC = main");
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
tryEval("PC after run2", "PC");
tryRead("GPADAT after main", 0x7F00);
tryRead("GPIO28/29 SCI check GPADAT bit28", 0x7F00);

log("=== set PC = sci_log_init symbol? try _c_int00 ===");
tryEval("PC = _c_int00", "PC = _c_int00");
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
tryEval("PC after run3", "PC");
tryRead("GPADAT after c_int00", 0x7F00);

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG5 DONE");
