// diag4.js - enumerate debug session options to find the auto-run setting

var ccxml = (arguments.length > 0) ? arguments[0] : null;

function log(s) { java.lang.System.out.println(s); }

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");

log("=== options matching 'run' ===");
debugSession.options.printOptions(".*[Rr]un.*");
log("=== options matching 'auto' ===");
debugSession.options.printOptions(".*[Aa]uto.*");
log("=== options matching 'load' ===");
debugSession.options.printOptions(".*[Ll]oad.*");
log("=== options matching 'connect' ===");
debugSession.options.printOptions(".*[Cc]onnect.*");

try { debugSession.target.disconnect(); } catch (e) { }
try { debugServer.stop(); } catch (e) { }
log("DIAG4 DONE");
