# HIL record - launchxl-f280039c-motor-pwm

- date: 2026-10-25 13:55:32
- operator: automated (run-hil.ps1 + dss hil_test_mp.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: E:\AIcherry\C2000TI\myPublicHarness\projects\launchxl-f280039c-motor-pwm\CPU1_FLASH\launchxl-f280039c-motor-pwm.out
- overall: **PASS**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | PASS |
| H2 flash programmed (code_start non-blank) | PASS |
| H3 power-on self test (g_self_test_code==0) | PASS |
| H4 PC inside application main loop | PASS |
| H5 ePWM/TZ/CMPSS register readback + forced over-limit trip and autonomous recovery | PASS |
| H6 left running standalone | PASS |

- g_self_test_code = 0
- PC = 0x83D18
- CMPA transition period: mean=233ms (expect ~100ms at the 10 Hz telemetry rate)
- fail reason: n/a

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-sci-2026-10-25_135329.log)

| expected line | found |
|---|---|
| banner | yes |
| sysclk=120MHz | yes |
| selftest-logic-OK | yes |
| pwm-regs-OK | yes |
| cmpss-bidir-OK | yes |
| tz-armed | yes |
| motor-line | yes |
| heartbeat-alive | yes |
| fault-injected-once | yes |
| fault-recovered | yes |
| no-spurious-fault | yes |

`
alive, ticks=92000
alive, ticks=93000
alive, ticks=1141000
alive, ticks=1142000
alive, ticks=1143000
alive, ticks=1144000
alive, ticks=1145000
=== LAUNCHXL-F280039C motor-pwm ===
sysclk : 120000000 Hz
self-test: svpwm/angle/fmt OK
alive, ticks=1000
alive, ticks=2000
`

## artifacts

- dss log: E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-dss-2026-10-25_135329.log
- sci log: E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-sci-2026-10-25_135329.log
