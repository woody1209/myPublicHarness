# tasks.md — 电机 PWM 骨架（无电机）

> 状态：`done` ｜ 上游：`requirements.md` / `design.md`

## 任务清单

- [x] **T-001**（REQ-001）实现 `board_pins.h` 引脚表：A/B/C 相 GPIO0/1、2/3、4/5（ePWM1/2/3 A/B）、
      ISENSE=A2、LED4/LED5=GPIO20/22、SCIA=GPIO28/29；只以 `BOARD_*_PIN/_PINCFG` 宏暴露
      - 文件：`projects/launchxl-f280039c-motor-pwm/source/board_pins.h`
      - 验证：`.\harness\tools\static-check.ps1 -Project launchxl-f280039c-motor-pwm`（引脚四规则 0 命中）
- [x] **T-002**（REQ-005）实现 `angle_gen` 纯逻辑模块（余数累积积分器、速率设置、回绕、边界）+ 主机单测
      - 文件：`source/angle_gen.c/.h`、`harness/tests/host/test/test_angle_gen.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-003**（REQ-006）实现 `svpwm` 纯逻辑模块（定点 sin/cos 表 + min-max 定心 + 过调制守卫）+ 主机单测
      - 文件：`source/svpwm.c/.h`、`harness/tests/host/test/test_svpwm.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-004**（REQ-002）实现 `board_epwm`：ePWM1/2/3 up-down 10kHz、互补 POLSEL=2、死区 120、
      SVPWM AQ 序列、时钟门控、GPIO 方向输出、寄存器回读打印
      - 文件：`source/board_epwm.c/.h`、`source/main.c`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm`
- [x] **T-005**（REQ-003）实现 `board_adc`：ADCA SOC0 ← EPWM1 SOCA、通道 A2、结果轮询
      - 文件：`source/board_adc.c/.h`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm`
- [x] **T-006**（REQ-004）实现 `board_cmpss`：CMPSS1 配置（正端=A2、负端=DAC shadow、DAC=4095）、
      XBAR TRIP4→ePWM1/2/3 DCBH→DCBEVT1 一次性锁存→TZA/TZB=LOW、COMPSTS/TZ 读写
      - 文件：`source/board_cmpss.c/.h`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm`
- [x] **T-007**（REQ-007）适配 `sci_log`/`fmt`（从 led-chaser 复制）并实现 motor:/alive 打点行
      - 文件：`source/sci_log.c/.h`、`source/fmt.c/.h`、`source/main.c`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm`
- [x] **T-008**（REQ-008）实现 `self_test`：纯逻辑参考序列 + 板级自检编排
      （pwm 回读 → CMPSS 双向 COMPSTS 探测 → 武装 TZ → svpwm/angle/fmt）+ fail-safe 双形态
      - 文件：`source/self_test.c/.h`、`source/main.c`
      - 验证：`.\harness\tools\run-host-tests.ps1` + `.\harness\tools\ccs.ps1 build`
- [x] **T-009**（REQ-001,002,003,004）工程基建：Makefile（cl2000 + `--args=0x200` + HEADERS 依赖）、
      链接脚本、`targetConfigs`（LaunchPad cJTAG ccxml）、`project.config.json`、`state.json`、工程 README
      - 文件：`projects/launchxl-f280039c-motor-pwm/**`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm`（FLASH + RAM）
- [x] **T-010**（REQ-005,006,008）主机单测多工程注册：modules.json v2 增加 motor-pwm 条目
      （logic=svpwm/angle_gen/fmt/self_test、runner=test_runner_mp.c）+ CMakeLists 双 runner + ctest
      - 文件：`harness/tests/host/modules.json`、`harness/tests/host/CMakeLists.txt`、`harness/tests/host/runner/test_runner_mp.c`、`harness/tests/host/test/test_mp_selftest.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`（聚合行 cases 覆盖两工程）+ `.\harness\tools\static-check.ps1`（M1/M2/M3 双腿）
- [x] **T-011**（REQ-002,003,004,007,008）HIL 扩展：`hil_test_mp.js` 增加 motor-pwm 判据
      （H5 ePWM/TZ/CMPSS 寄存器回读 + DAC 强制越限实验 + TZ 锁存/清除），跑 `run-hil.ps1` 九腿
      - 文件：`harness/tools/dss/hil_test_mp.js`、`harness/tools/run-hil.ps1`（已参数化，补齐契约核对）
      - 验证：`.\harness\tools\run-hil.ps1 -Project projects\launchxl-f280039c-motor-pwm`
- [x] **T-012**（全部 REQ）收尾：`ccs.ps1 size`、回填 `trace.json` 与 `state.json`、
      更新 target card / on-target README / harness 文档索引 / 记忆
      - 文件：`specs/0003-motor-pwm-skeleton/trace.json`、`projects/launchxl-f280039c-motor-pwm/state.json`、`targets/F280039C.json`、`harness/tests/on-target/README.md`
      - 验证：`.\harness\tools\spec.ps1 check -Id 0003-motor-pwm-skeleton`

## 备注

- 启动顺序沿用 led-chaser 已验证硬顺序（ADR 0003）：ramfunc relocate+ready → 禁看门狗 →
  PIE init → 时钟 → 外使能时钟门控（ADR 0005）→ 自检 → superloop。
- `source/fmt.c/.h`、`source/sci_log.c/.h` 从 led-chaser **原样复制**（已上板验证，零改动即零风险）；
  static-check 的 LAY-DRIVERLIB-IN-LOGIC 规则只约束 logic[] 申报模块，fmt/self_test 保持纯净。
- CMPSS 自检在 TZ 链武装**之前**执行（REQ-004 第五条 EARS），避免阈值穿越误触发。
- 定点 sin/cos 表由生成脚本产出后随源码提交，禁止手抄（901 项四分之一波表，1802 B）。
