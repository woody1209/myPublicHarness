// diag18.js - boot our flash image by forcing PC=0x80000 (no symbols needed),
// then report SCI/GPIO truth while the firmware runs. Pairs with diag18.ps1
// which sniffs COM5 + COM6 simultaneously.
// Usage: dss.bat diag18.js <ccxml>

var ccxml = arguments[0];

function log(s) { java.lang.System.out.println(s); }
function sleep(ms) { java.lang.Thread.sleep(ms); }
function hex(n) { return '0x' + (n < 0 ? 'ERR' : (n >>> 0).toString(16).toUpperCase()); }
function readW(addr) {
    for (var a = 0; a < 3; a++) {
        try { return Number(debugSession.memory.readWord(0, addr)); } catch (ex) { sleep(30); }
    }
    return -1;
}
function w32(lo) {
    var l = readW(lo), h = readW(lo + 1);
    return (l < 0 || h < 0) ? -1 : ((h << 16) | l) >>> 0;
}
function haltAndWait() {
    try { debugSession.target.halt(); } catch (ex) { }
    for (var w = 0; w < 3000; w += 20) { if (debugSession.target.isHalted()) { return true; } sleep(20); }
    return debugSession.target.isHalted();
}

var dssScriptEnv = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment.instance();
dssScriptEnv.setScriptTimeout(-1);
var debugServer = dssScriptEnv.getServer("DebugServer.1");
debugServer.setConfig(ccxml);
var debugSession = debugServer.openSession("*", "*");
try { debugSession.options.setBoolean("AutoRunToLabelOnRestart", false); } catch (ex) { }
debugSession.target.connect();
try { debugSession.expression.evaluate("GEL_MapOff()"); } catch (ex) { }
if (!haltAndWait()) { log('cannot halt'); java.lang.System.exit(1); }

log('entry word @0x80000 = ' + hex(readW(0x80000)) + ' (expect 0x48 = LCR)');

// cold-boot into our application exactly like the boot ROM does after CR load
debugSession.expression.evaluate("PC = 0x80000");
debugSession.target.runAsynch();
log('started from 0x80000, running for firmware boot + banner + 2 chaser steps');
sleep(1500);

if (!haltAndWait()) { log('halt #1 failed'); }
var p = Number(debugSession.expression.evaluate("PC"));
log('after 1.5 s: PC=' + hex(p) + (p >= 0x80000 && p < 0x88000 ? ' [APP FLASH]' :
    p >= 0x8000 && p < 0xB000 ? ' [RAM]' : p >= 0x3F0000 ? ' [BOOT ROM!]' : ' [?]'));
log('  g_self_test_code=0x' + readW(0xA80C).toString(16));
log('  SCIA CTL1=' + hex(readW(0x7201)) + ' (want 0xB=RX|TX|SWRESET)  CCR=' + hex(readW(0x7200)) +
    '  CTL2=' + hex(readW(0x7204)) + '  RXST=' + hex(readW(0x7205)));
log('  SCIA HBAUD=' + hex(readW(0x7202)) + ' LBAUD=' + hex(readW(0x7203)) +
    '  FFTX=' + hex(readW(0x720A)) + ' (want SCIFFENA bit14=1)  FFCT=' + hex(readW(0x720C)));
log('  PCLKCR7=' + hex(w32(0x5D318)) + ' (bit0 SCIA clock)');
log('  GPIO mux28/29 (want 1) = ' + ((w32(0x7C08) >>> 24) & 3) + '/' + ((w32(0x7C08) >>> 26) & 3) +
    '  DIR=' + ((w32(0x7C0A) >>> 26) & 3) + ' (29 out)  GMUX29=' + ((w32(0x7C1E) >>> 13) & 6) );
log('  GPADAT=' + hex(w32(0x7F00)) + '  GPADAT_R=' + hex(w32(0x7F80)) +
    '  MUX2=' + hex(w32(0x7C08)) + '  DIR=' + hex(w32(0x7C0A)) + '  PUD=' + hex(w32(0x7C0C)));
var tcr = readW(0x0C04);
log('  CPUT0 PRD=' + hex(w32(0x0C02)) + ' TCR=' + hex(tcr) + ' (ICEM bits10-8: ' + ((tcr >>> 8) & 7) +
    ', 2=STOPATZERO 4=RUNFREE) TPR=' + hex(readW(0x0C06)));

// resume and let it chase + print for the serial sniffers, then detach live
debugSession.target.runAsynch();
sleep(6000);
log('left running: PC check + final LED state');
if (haltAndWait()) {
    log('  PC now=' + hex(Number(debugSession.expression.evaluate("PC"))));
    log('  GPADAT=' + hex(w32(0x7F00)) + ' GPADAT_R=' + hex(w32(0x7F80)));
    debugSession.target.runAsynch();
    sleep(100);
}
try { debugSession.target.disconnect(); } catch (ex) { log('disconnect: ' + ex); }
debugServer.stop();
log('DIAG18 DONE - board left RUNNING');
java.lang.System.exit(0);
