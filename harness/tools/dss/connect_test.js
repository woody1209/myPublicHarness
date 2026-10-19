// connect_test.js - DSS read-only JTAG connection test (no load, no erase)
//
// Purpose: prove the debug path (XDS110 -> cJTAG -> C28xx) actually reaches the
// MCU without modifying anything on the target. This is the first gate before
// any flash operation: a connect failure here must be fixed (cable, power,
// shunts, ccxml) before 'ccs.ps1 program' is even attempted.
//
// Run:
//   dss.bat connect_test.js <path-to-ccxml>
// e.g.
//   D:\ti\ccs2101\ccs\ccs_base\scripting\bin\dss.bat connect_test.js ^
//     E:\...\projects\launchxl-f280039c-led-chaser\targetConfigs\TMS320F280039C_LaunchPad.ccxml
//
// Exit codes are reported through printed text; DSS itself exits 0 unless the
// script throws.

var ccxml = arguments.length > 0 ? arguments[0] : null;

if (ccxml == null) {
    print("usage: dss.bat connect_test.js <ccxml>");
    throw new Error("missing ccxml argument");
}

print("ccxml : " + ccxml);

var ds = script.getServer("DebugServer.1");
ds.setConfig(ccxml);

var session = ds.openSession("*", "*");
print("session: " + session.getName());

session.target.connect();

var connected = session.target.isConnected();
print("connected: " + connected);

if (!connected) {
    session.target.disconnect();
    ds.stop();
    throw new Error("target did not connect - check power, shunts, cJTAG ccxml");
}

// Light-touch read-only probes. These must never write to the device.
var GPIODATA_BASE = 0x00007F00;   // f28003x hw_memmap.h
try {
    var gpadat = session.memory.readWord(0, GPIODATA_BASE);
    print("GPADAT  : 0x" + gpadat.toString(16).toUpperCase());
} catch (e) {
    print("GPADAT  : <unreadable> " + e);
}

session.target.disconnect();
ds.stop();
print("CONNECT TEST OK (read-only, nothing written)");
