# HIL record - launchxl-f280039c-full-platform

- date: 2026-10-30 06:17:02
- operator: automated (run-hil.ps1 + dss hil_test.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-full-platform\CPU1_FLASH\launchxl-f280039c-full-platform.out
- overall: **FAIL**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | PASS |
| H2 flash programmed (code_start non-blank) | PASS |
| H3 power-on self test (g_self_test_code==0) | PASS |
| H4 PC inside application main loop | FAIL |
| H5 LED polarity + 200ms cadence | FAIL |
| H6 left running standalone | FAIL |

- g_self_test_code = 0
- 
- 
- fail reason: H5-cadence (mean 397.3ms vs expected 200ms)

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-30_061501.log)

| expected line | found |
|---|---|
| banner | yes |
| sysclk=120MHz | yes |
| selftest-pattern-OK | yes |
| selftest-fmt-OK | yes |
| selftest-scheduler-OK | yes |
| selftest-health-OK | yes |
| health-line | yes |
| m2-line | yes |

`
health: up=60 map=0x0x2090 err=640 hb=58 step=302, mode=0
health: up=65 map=0x0x2090 err=695 hb=63 step=327, mode=0
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
self-test: scheduler OK
self-test: health OK
health: up=0 map=0x0x10 err=1 hb=0 step=2, mode=0
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
`

## artifacts

- dss log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-dss-2026-10-30_061501.log
- sci log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-30_061501.log
