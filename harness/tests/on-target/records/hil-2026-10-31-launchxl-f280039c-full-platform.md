# HIL record - launchxl-f280039c-full-platform

- date: 2026-10-31 23:59:40
- operator: automated (run-hil.ps1 + dss hil_test_fp.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: E:\AIcherry\C2000TI\myPublicHarness\projects\launchxl-f280039c-full-platform\CPU1_FLASH\launchxl-f280039c-full-platform.out
- overall: **PASS**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | PASS |
| H2 flash programmed (code_start non-blank, 5 HIL symbols resolve) | PASS |
| H3 power-on self test (g_self_test_code==0) | PASS |
| H4 PC inside application main loop | PASS |
| H5 halt-free liveness: watchdog service advances, no reset, subsystem status non-zero | PASS |
| H6 left running standalone | PASS |

- H3 PASS: g_self_test_code == 0 (self test passed on target)
- 
- 
- fail reason: n/a

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-sci-2026-10-31_235805.log)

| expected line | found |
|---|---|
| banner | yes |
| sysclk=120MHz | yes |
| selftest-pattern-OK | yes |
| selftest-fmt-OK | yes |
| selftest-sched-OK | yes |
| selftest-health-OK | yes |
| health-line | yes |
| no-selftest-fail | yes |

```
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
self-test: scheduler OK
self-test: health OK
sysclk : 120000000 Hz
self-test: pattern OK
self-test: fmt OK
self-test: scheduler OK
self-test: health OK
```

## artifacts

- dss log: E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-dss-2026-10-31_235805.log
- sci log: E:\AIcherry\C2000TI\myPublicHarness\harness\tests\on-target\records\hil-sci-2026-10-31_235805.log
