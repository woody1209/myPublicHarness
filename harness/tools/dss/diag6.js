// diag6.js - read RESC (reset cause) and the boot ROM code at the stuck PC

var ccxml = (arguments.length > 0) ? arguments[0] : null;
var outFile = (arguments.length > 1) ? arguments[1] : null;

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function tryRead(tag, addr) {
    try {
        var v = Number(debugSession.memory.readWord(0, addr));
        log("  READ " + tag + " (0x" + addr.toString(16) + ") = 0x" + v.toString(16).toUpperCase());
        return v;
    } catch (ex) { log("  READ " + tag + " (0x" + addr.toString(16) + ") FAILED: " + ex); return null; }
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
debugSession.target.connect();
debugSession.options.setBoolean("AutoRunToLabelOnRestart", false);
debugSession.expression.evaluate("GEL_MapOff()");

log("=== RESC (reset cause) before load ===");
tryRead("RESCCLR", 0x5D400);
tryRead("RESC lo", 0x5D404);
tryRead("RESC hi", 0x5D405);
tryRead("SIMRESET", 0x5D200 + 0xE4);   // SIMRESET offset 228 = 0xE4

log("=== load + run from code_start ===");
debugSession.memory.loadProgram(outFile);
debugSession.expression.evaluate("PC = code_start");
debugSession.target.runAsynch();
sleep(1500);
debugSession.target.halt();
sleep(100);
var pc = debugSession.expression.evaluate("PC");
log("PC after run = 0x" + Number(pc).toString(16).toUpperCase());

log("=== RESC after the run ===");
tryRead("RESC lo", 0x5D404);
tryRead("RESC hi", 0x5D405);

log("=== boot ROM dump around 0x3FF1F8 ===");
var base = 0x3FF1E0;
for (var i = 0; i < 12; i++) {
    tryRead("ROM+0x" + (base + i - 0x3FF1F8), base + i);
}

log("=== boot ROM dump at 0x3FF800 (ROM start) ===");
for (var j = 0; j < 6; j++) {
    tryRead("ROMstart+" + j, 0x3FF800 + j);
}

log("=== our flash code_start check ===");
tryRead("flash 0x80000", 0x80000);
tryRead("flash 0x80001", 0x80001);
tryRead("wd_disable 0x82DD1", 0x82DD1);
tryRead("wd_disable 0x82DD2", 0x82DD2);

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG6 DONE");
