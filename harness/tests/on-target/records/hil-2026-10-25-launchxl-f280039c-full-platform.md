# HIL record - launchxl-f280039c-full-platform

- date: 2026-10-25 02:46:31
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
- PC = 0x82C26
- 
- fail reason: n/a

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-25_024500.log)

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
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
self-test: scheduler OK
self-test: health OK
health: up=0 map=0x0x10 err=1 hb=0 step=2, mode=0
health: up=5 map=0x0x90 err=8 hb=3 step=27, mode=0
=== LAUNCHXL-F280039C full-platform ===
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
`

## artifacts

- dss log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-dss-2026-10-25_024500.log
- sci log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-25_024500.log
