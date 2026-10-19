# tasks.md — LED 流水灯（led-chaser）

> 状态：`implementing` ｜ 上游：`requirements.md` / `design.md`

## 任务清单

- [x] **T-001**（REQ-001）实现 `led_pattern` 纯逻辑模块（三种模式、回绕/折返、边界、极性映射）
      - 文件：`projects/launchxl-f280039c-led-chaser/source/led_pattern.c`、`source/led_pattern.h`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-002**（REQ-005）实现 `fmt` 纯逻辑格式化模块（`fmt_u32_dec` / `fmt_u32_hex`）
      - 文件：`projects/launchxl-f280039c-led-chaser/source/fmt.c`、`source/fmt.h`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-003**（REQ-002）绑定板级引脚并实现 `led_hw`（active-low 写电平）
      - 文件：`projects/launchxl-f280039c-led-chaser/source/board_pins.h`、`source/led_hw.c/.h`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser`
- [x] **T-004**（REQ-003）实现 `board_clock`（INTOSC2 → 120MHz + 校验）与 CPU Timer 节拍
      - 文件：`projects/launchxl-f280039c-led-chaser/source/board_clock.c/.h`、`source/main.c`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser`
- [x] **T-005**（REQ-005）实现 `sci_log`（SCIA 115200-8N1 轮询发送）并接入 main
      - 文件：`projects/launchxl-f280039c-led-chaser/source/sci_log.c/.h`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser`
- [x] **T-006**（REQ-004）实现上电自检 `self_test`（共享参考常量 + fail-safe 形态）
      - 文件：`projects/launchxl-f280039c-led-chaser/source/self_test.c/.h`
      - 验证：`.\harness\tools\run-host-tests.ps1` + 上板 fail-safe 故障注入
- [x] **T-007**（REQ-001, REQ-002, REQ-003）工程基建：Makefile（cl2000 + 双配置）、链接脚本、
      `targetConfigs`（LaunchPad cJTAG ccxml）、`project.config.json`、`state.json`、工程 README
      - 文件：`projects/launchxl-f280039c-led-chaser/**`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser`（FLASH + RAM）
- [x] **T-008**（REQ-001, REQ-002, REQ-004, REQ-005）编写主机侧单测并注册进 CMakeLists
      - 文件：`harness/tests/host/test/test_led_pattern.c`、`harness/tests/host/test/test_fmt.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-009**（REQ-001, REQ-002, REQ-003, REQ-004, REQ-005）跑 `ccs.ps1 size`、
      更新 target card/docs、回填 `trace.json` 与 `state.json`
      - 验证：`.\harness\tools\spec.ps1 check -Id 0001-led-chaser`

## 备注

- 节拍先采用 **CPU Timer 轮询**（非中断）：首版降低复杂度，且 CPU Timer 溢出仍由硬件给出，
  不依赖软件延时精度；切入中断属于 M 级变更，需另立 spec。
- 参考常量放在 `led_pattern.h`，主机单测与目标自检共用同一份（REQ-004 要求"不允许两份真相"）。
