// diag20_reset_boot.js - does the FLASHED image boot by itself after a reset?
//
// Why: every HIL run forces "PC = code_start" through the debugger (H6). That
// proves the flash CONTENTS are good, but not that the on-chip boot ROM hands
// off to flash. diag17 (2026-10-17) saw the boot ROM park at 0x3FB8B9 (SCI
// wait) after target.reset() while the image on flash was still the
// watchdog-tripping one. Re-test now that the watchdog is disabled early and
// the SCI clock gate is fixed.
//
// Usage: dss.bat diag20_reset_boot.js <ccxml>
var ccxml = arguments[0];

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function hex(n) { return '0x' + (n < 0 ? 'ERR' : (n >>> 0).toString(16).toUpperCase()); }
function rd(a) {
    for (var i = 0; i < 3; i++) {
        try { return Number(debugSession.memory.readWord(0, a)); } catch (ex) { }
    }
    return -1;
}
function haltPc() {
    try {
        debugSession.target.halt();
        var w = 0;
        while (w < 2000 && !debugSession.target.isHalted()) { sleep(10); w += 10; }
        return Number(debugSession.expression.evaluate("PC"));
    } catch (ex) { return -1; }
}
function region(pc) {
    if (pc < 0) { return 'READ-FAILED'; }
    if (pc >= 0x3F8000) { return 'BOOT ROM'; }
    if (pc >= 0x80000 && pc <= 0x8FFFF) { return 'APP FLASH'; }
    return 'other';
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
try { debugSession.options.setBoolean("AutoRunToLabelOnRestart", false); } catch (ex) { }
debugSession.target.connect();
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }

log('--- reset vector (halted, right after target.reset()) ---');
try { debugSession.target.reset(); } catch (ex) { log('reset failed: ' + ex); }
sleep(200);
log('  reset PC        = ' + hex(haltPc()));

log('--- release into the boot ROM, watch for handoff to flash ---');
debugSession.target.runAsynch();
var checkpoints = [300, 800, 1500, 3000, 5000];
var tPrev = 0;
for (var k = 0; k < checkpoints.length; k++) {
    sleep(checkpoints[k] - tPrev);
    tPrev = checkpoints[k];
    var pc = haltPc();
    log('  t=+' + checkpoints[k] + 'ms PC=' + hex(pc) + ' [' + region(pc) + ']');
    debugSession.target.runAsynch();
}

// Halt-free proof of life: a running chaser flips the LED pair at 200 ms.
sleep(200);
var LED_MASK_HI = (1 << 4) | (1 << 6);   // GPIO20 / GPIO22 inside GPADAT_R high word
var start = new Date().getTime(), prevBits = -1, transitions = 0, times = [];
while (new Date().getTime() - start < 2600) {
    var hi = rd(0x7F81);
    if (hi < 0) { continue; }
    var bits = hi & LED_MASK_HI;
    if (prevBits >= 0 && bits !== prevBits) {
        transitions++;
        times.push(new Date().getTime() - start);
    }
    prevBits = bits;
    sleep(20);
}
log('  halt-free LED transitions in 2.6 s = ' + transitions +
    '  at t(ms): ' + times.join(','));
log('  (>=10 transitions => app is chasing at ~200 ms; 0 => boot ROM never handed off)');
log('DIAG20 DONE - board left RUNNING');

try { debugSession.target.disconnect(); } catch (ex) { log('disconnect: ' + ex); }
debugServer.stop();
java.lang.System.exit(0);
