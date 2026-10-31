# tasks.md — 全外设并存平台工程（full-platform）

> 状态：`implementing` ｜ 上游：`requirements.md` / `design.md`（里程碑划分见 design.md §11）

## 任务清单（按里程碑 M1–M5 排列，每步 verify 全绿再进下一步）

### M1 工程骨架与调度/健康内核

- [x] **T-001**（REQ-001, REQ-003, REQ-020, REQ-022）工程基建：Makefile（拷参考工程，保留
      `--args=0x200`/HEADERS 依赖/cmd.exe 配方）、flash/ram 链接脚本（基自 led-chaser，预留 CLA 段）、
      `device/`、ccxml、`project.config.json`、`state.json`、`board_pins.h`（full-platform 全量引脚矩阵，design.md §6）
      - 文件：`projects/launchxl-f280039c-full-platform/**`
      - 验证：`.\harness\tools\ccs.ps1 build -Project launchxl-f280039c-full-platform`
- [x] **T-002**（REQ-003）实现 `scheduler` 纯逻辑模块（任务表/到期/错峰/迟到统计）
      - 文件：`projects/launchxl-f280039c-full-platform/source/scheduler.c/.h`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-003**（REQ-020）实现 `health` 纯逻辑模块（心跳/错误/位图/阈值/fail-safe 判定）
      - 文件：`projects/launchxl-f280039c-full-platform/source/health.c/.h`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-004**（REQ-001, REQ-020）移植基础设施模块（board_clock/board_error/sci_log/fmt/led_pattern/led_hw，
      自 led-chaser 原样拷贝，工程自包含）+ 新 `main.c`（启动硬约束 + 调度循环 + 5s 健康行）+
      扩展 `self_test`（pattern/fmt/scheduler/health 参考不变量）
      - 文件：`projects/launchxl-f280039c-full-platform/source/**`
      - 验证：`.\harness\tools\ccs.ps1 build -Project launchxl-f280039c-full-platform`
- [x] **T-005**（REQ-001）harness 多工程化：modules.json v2（按工程分组）+ static-check/run-host-tests/
      CMakeLists/test_runner 适配，led-chaser 用例零语义变化
      - 文件：`harness/tests/host/**`、`harness/tools/static-check.ps1`、`harness/tools/run-host-tests.ps1`
      - 验证：led-chaser 回归 verify 全绿 + full-platform host-tests 全绿
- [x] **T-006**（REQ-001, REQ-003, REQ-020）主机单测 `test_scheduler.c` / `test_health.c` 并入 runner
      - 文件：`harness/tests/host/test/test_scheduler.c`、`harness/tests/host/test/test_health.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [x] **T-007**（REQ-001）M1 收口：`ccs.ps1 size` 基线登记 state.json、工程 README、
      trace.json 填 M1 证据
      - 验证：`.\harness\tools\verify.ps1 -Project launchxl-f280039c-full-platform`（软件腿全绿）

### M2 采集与控制链（REQ-002, 006, 007, 008, 009, 010）

- [x] **T-008**（REQ-002）`dcc_mon` 子系统（DCC0 时钟持续校验 + 健康计数）
- [x] **T-009**（REQ-006）`adc_scan` 子系统（3 ADC×2 SOC 轮询 + `adc_stat` 纯逻辑统计模块 + 主机单测）
- [x] **T-010**（REQ-007）`dac_cmpss` 子系统（DACA 阶梯→ADCA 回环比对、DACB→ADCC、CMPSS1+TZ；
      DAC 回环通道号实证并回写 design/targets 卡）
- [x] **T-011**（REQ-008）`pwm_bank` 子系统（8 模块 16 通道、4 频率组、HRPWM@EPWM3、TZ@EPWM1）
- [x] **T-012**（REQ-009）`cap_meas` 子系统（eCAP×3 经 INPUT7/8/9 回灌测频/占空比、HRCAP@ECAP3、
      `math_ref` 参考与带宽判定 + 主机单测）
- [x] **T-013**（REQ-010）`qep_quadv` 子系统（eQEP×2 正交回灌、位置/方向/速度判读）
- [x] **T-014**（REQ-002, 006–010）M2 收口：build/size/host-tests/trace 回填、健康行扩展字段实证

### M3 通信与 DMA（REQ-011, 012, 013, 014, 015, 016）

- [x] **T-015**（REQ-011）`frame_gen` 纯逻辑模块（SPI/MCAN/FSI/LIN 测试帧 + 参考校验值 + 主机单测）
- [x] **T-016**（REQ-011）`spi_loop` 子系统（SPIA/SPIB 数字回环 + 逐字节校验）
- [x] **T-017**（REQ-012）`i2c_pair` 子系统（I2CA 内部回环主判据；A↔B 跳线物理层实证并登记结论）
- [x] **T-018**（REQ-013）`serial_loop` 子系统（SCIB/LINA/LINB 内部回环）
- [x] **T-019**（REQ-014）`mcan_loop` 子系统（MCANA 自测回环 std+ext 帧）
- [x] **T-020**（REQ-015）`fsi_loop` 子系统（RX 内部回环主判据；跳线可选实证）
- [x] **T-021**（REQ-016）`dma_bank` 子系统（6 通道：ADC/SPIB TX·RX/MCAN RX/内存块/LINA RX）
- [x] **T-022**（REQ-011–016）M3 收口：健康行通信字段 + trace 回填

### M4 加速器与安全（REQ-004, 005, 017, 018, 019）

- [ ] **T-023**（REQ-004）CLA：链接段落地（Cla1Prog RUN=RAMLS1）+ `app_cla.cla` 周期任务 + 消息 RAM 交换 + CPU 参考比对
- [ ] **T-024**（REQ-005）`accel` 子系统（VCRC 自检 CRC、TMU vs math_ref、AES 加解密往返、BGCRC 增量窗口）
- [ ] **T-025**（REQ-017）`clb_demo` 子系统（clb.h 寄存器级 CLB1 计数器；选型结论记 ADR）
- [ ] **T-026**（REQ-018）窗口看门狗启用（init 完成后使能、主循环按窗喂、RESC 解码进 banner）
- [ ] **T-027**（REQ-019）EPG 注入尝试或登记（open_questions 回写）
- [ ] **T-028**（REQ-004, 005, 017–019）M4 收口

### M5 HIL 扩展、soak 与收口（REQ-020, 021）

- [ ] **T-029**（REQ-021）hil_test.js 增 H7–H11（健康字/eCAP 频率/eQEP/DMA/通信簇）+ 负向自测（ADR 0009）
- [ ] **T-030**（REQ-021）run-hil.ps1 `-SoakMinutes` 30min soak 模式 + 证据落 records/
- [ ] **T-031**（REQ-018, 020）故障注入验收（停喂狗→WDRSn 复位判读；坏参考常量→fail-safe，dis2000 取证）
- [ ] **T-032**（REQ-001–022）总收口：`verify.ps1 -Hil` 全绿、`ccs.ps1 program` 留证、
      trace 全 verified、ADR（CLB 选型 + 新坑）、docs/10 踩坑、根/工程 README、targets 卡回填

## 备注

- 需求措辞已按硅片事实修正（无 SCIC；ePWM=8 模块 16 通道；CLB=4 tiles；DAC=DACA/DACB 独立外设），见 design.md §2。
- 参考工程 launchxl-f280039c-led-chaser 的任何文件不修改（REQ-001）；harness/tests、harness/tools 属共享设施，允许扩展但必须过 led-chaser 回归门。
- 每个里程碑收口必须：`verify.ps1 -Project launchxl-f280039c-full-platform` 软件腿全绿 + led-chaser 回归全绿。
