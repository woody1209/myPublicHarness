# HIL record - launchxl-f280039c-led-chaser

- date: 2026-10-24 23:27:59
- operator: automated (run-hil.ps1 + dss hil_test.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-full-platform\CPU1_FLASH\launchxl-f280039c-full-platform.out
- overall: **PASS**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | PASS |
| H2 flash programmed (code_start non-blank) | PASS |
| H3 power-on self test (g_self_test_code==0) | PASS |
| H4 PC inside application main loop | PASS |
| H5 LED polarity + 200ms cadence | PASS |
| H6 left running standalone | PASS |

- g_self_test_code = 0
- PC = 0x826B5
- 
- fail reason: n/a

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-24_232628.log)

| expected line | found |
|---|---|
| banner | yes |
| sysclk=120MHz | yes |
| selftest-pattern-OK | yes |
| selftest-fmt-OK | yes |
| selftest-scheduler-OK | yes |
| selftest-health-OK | yes |
| health-line | yes |

`
health: up=80 map=0x0x0 err=0 hb=402 step=402, mode=0
health: up=85 map=0x0x0 err=0 hb=427 step=427, mode=0
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
self-test: scheduler OK
self-test: health OK
health: up=0 map=0x0x0 err=0 hb=0 step=2, mode=0
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
`

## artifacts

- dss log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-dss-2026-10-24_232628.log
- sci log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-24_232628.log
