# HIL record - launchxl-f280039c-led-chaser

- date: 2026-10-18 04:20:49
- operator: automated (run-hil.ps1 + dss hil_test.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: E:\AIcherry\C2000TI\F280039C-harness\projects\launchxl-f280039c-led-chaser\CPU1_FLASH\launchxl-f280039c-led-chaser.out
- overall: **FAIL**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | PASS |
| H2 flash programmed (code_start non-blank) | PASS |
| H3 power-on self test (g_self_test_code==0) | FAIL |
| H4 PC inside application main loop | FAIL |
| H5 LED polarity + 200ms cadence | FAIL |
| H6 left running standalone | FAIL |

- g_self_test_code = 1
- 
- 
- fail reason: H3-selftest (code=1, firmware is in fail-safe)

## SCI console log (COM5)

capture: OK (E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-18_041918.log)

| expected line | found |
|---|---|
| boot banner | yes |
| sysclk 120000000 Hz | yes |
| step 200 ms | yes |
| self-test: pattern OK | NO |
| self-test: fmt OK | NO |
| alive, steps= heartbeat | yes |

`
alive, steps=400, mode=0
alive, steps=425, mode=0
alive, steps=450, mode=0
alive, steps=475, mode=0
alive, steps=500, mode=0
alive, steps=525, mode=0
alive, steps=550, mode=0
alive, steps=575, mode=0
alive, steps=600, mode=0
alive, steps=625, mode=0
alive, steps=650, mode=0
alive, steps=675, mode=0
`

## artifacts

- dss log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-dss-2026-10-18_041918.log
- sci log: E:\AIcherry\C2000TI\F280039C-harness\harness\tests\on-target\records\hil-sci-2026-10-18_041918.log
