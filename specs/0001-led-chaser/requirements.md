# requirements.md — LED 流水灯（led-chaser）

> 状态：`requirements-approved` ｜ 关联 ADR：`0001-平台与目标选型.md`
> 机读镜像：`spec.json`
> 目标工程：`projects/launchxl-f280039c-led-chaser`

## 1. 简介

在 **LAUNCHXL-F280039C** 上实现 LED 流水灯：两颗板载用户 LED（LED4 红 = GPIO20，
LED5 绿 = GPIO22，低电平点亮）按可配置节拍与 Flow 模式点亮。同时提供**上电自检**
（pattern 引擎与格式化模块的参考序列校验）、**fail-safe 故障形态**与 **SCI 日志**，
使该工程具备"看得见的正确性"，并作为本 harness 上后续所有板级软件变更的基线。

## 2. 术语表

| 术语 | 含义 |
|---|---|
| LED4 / LED5 | LaunchPad 用户 LED，分别接 GPIO20 / GPIO22（TI SPRUJ31 §3.1.3） |
| active-low | 低电平点亮：GPIO 输出 0 → LED 亮；输出 1 → LED 灭 |
| channel | 流水灯通道（0 = LED4，1 = LED5）；引擎本身支持至 32 通道，工程实例化为 2 |
| mask | 位掩码，bit i = 1 表示第 i 个通道"逻辑点亮" |
| step | 节拍计数，每次递增驱动 pattern 前进一格 |
| fail-safe | 自检失败后的固定形态：双灯以 100ms 周期同拍闪烁，且不再推进 pattern |

## 3. 需求

### REQ-001 流水图样引擎（纯逻辑）

- **用户故事**：作为固件开发者，我想要一个与硬件无关的 pattern 引擎，
  以便在主机侧用单元测试完整覆盖其序列正确性，也不需要接板就能验证行为。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 提供 `led_pattern_mask(mode, channel_count, step)`，按模式与节拍序号返回点亮掩码，且不依赖任何硬件或全局状态。
  - WHEN 模式为 `CHASE` 且 `step` 递增到 `channel_count` 的整数倍，THE SYSTEM SHALL 将点亮位置回绕到第 0 通道。
  - WHEN 模式为 `BOUNCE`，THE SYSTEM SHALL 使点亮位置到达末通道后反向回走到第 0 通道（周期 2·n−2）。
  - WHEN 模式为 `BLINK`，THE SYSTEM SHALL 在相邻节拍交替输出"全通道点亮"与"全通道熄灭"。
  - IF `channel_count` 为 0 或大于 `LED_PATTERN_MAX_CHANNELS`(32)，THEN THE SYSTEM SHALL 返回掩码 0 且不产生未定义行为。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：`test_led_pattern.c` 中三种模式的前 8 拍掩码逐项断言全部命中
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser` 成功

### REQ-002 板级引脚绑定与极性映射

- **用户故事**：作为板级开发者，我想要 GPIO 引脚与点灯极性只在唯一一处定义，
  以便将来换板或改写 noexcept 引脚时不会出现"多个真相"。
- **优先级**：`must`
- **类别**：`interface`
- **EARS 验收**：
  - THE SYSTEM SHALL 将 LED4 绑定到 GPIO20、LED5 绑定到 GPIO22，且该绑定只在 `board_pins.h` 出现一次。
  - WHEN 掩码某位为逻辑点亮(1)，THE SYSTEM SHALL 因 active-low 而向对应 GPIO 写入低电平(0)。
  - WHEN 掩码某位为逻辑熄灭(0)，THE SYSTEM SHALL 向对应 GPIO 写入高电平(1)。
  - THE SYSTEM SHALL 不得将 GPIO35–38 配置为 GPIO 功能（保护 JTAG）。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：`test_led_pattern.c` 中 `led_pattern_levels()` 极性用例通过
  - 上板自检：单亮 LED4 时测得 GPIO20 ≈ 0V、GPIO22 ≈ 3.3V（`harness/tests/on-target/README.md`）

### REQ-003 节拍时序

- **用户故事**：作为验证者，我想要流水灯步进周期稳定且可预测，以便用板上 JTAG peek 与（延后的）外部时基仪器两级判据来认定时序达标。
- **优先级**：`must`
- **类别**：`performance`
- **EARS 验收**：
  - THE SYSTEM SHALL 在系统时钟 120MHz 下以默认 `200ms` 为步进周期推进流水灯，且该周期可由板上 HIL 在 `±25%` 带宽内裁决（`harness/tools/dss/hil_test.js` 的 `CADENCE_TOL = 0.25`）。
  - THE SYSTEM SHALL 使步进周期进一步落在 `200ms ±2%` 内；该带宽必须由外部时基仪器（逻辑分析仪/示波器）裁决，2026-10-18 经用户决定延后接入仪器，因此本条不进入当前门禁判据（见 §5 未决项 3）。
  - THE SYSTEM SHALL 使用 CPU Timer 计时源（`CPUTimer`），并把溢出计数转换为节拍，不得依赖软件忙等近似值。
  - WHEN 修改编译期常量 `LEDCHASER_STEP_MS`，THE SYSTEM SHALL 使步进周期随之线性变化。
  - IF 系统时钟未按预期跑到 120MHz，THEN THE SYSTEM SHALL 进入 fail-safe 形态并在日志打印错误信息。
- **可执行验收**：
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser` 成功且 `board_clock.c` 链接
  - `.\harness\tools\run-hil.ps1` 的 H5：halt-free peek 得到的相邻边沿间隔均值落在 200ms ±25% 内，且跳变样本 ≥ 8
  - （延后，非当前门禁）逻辑分析仪测 GPIO20 相邻边沿间隔 = 200ms ± 2% —— 仪器未接入，不据此判定通过

### REQ-004 上电自检与 fail-safe

- **用户故事**：作为用这套 harness 长期维护该板软件的人，我想要每次上电自动校验核心逻辑，
  以便在后续任何改动引入回归时第一时间发现"核心逻辑被改坏"。
- **优先级**：`must`
- **类别**：`constraint`
- **EARS 验收**：
  - WHEN 上电复位完成，THE SYSTEM SHALL 在启动 banner 之后执行 pattern 序列校验与格式化校验，并向 SCI 打印每条结果。
  - IF 自检全部通过，THEN THE SYSTEM SHALL 进入 normal 形态并按 REQ-003 推进流水灯。
  - IF 任一项自检失败，THEN THE SYSTEM SHALL 进入 fail-safe（双灯 100ms 同拍闪烁）、打印 `self-test FAIL`、并保留失败项编号供调试器读取。
  - THE SYSTEM SHALL 使主机侧单测与目标侧自检引用同一组参考常量（同一头文件），不允许出现两份不一致的参考值。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：参考序列/参考校验值用例通过
  - 上板：串口出现 `self-test: pattern OK` 与 `self-test: fmt OK`；故障注入后转为 fail-safe 形态

### REQ-005 SCI 日志输出

- **用户故事**：作为调试者，我想要通过 XDS110 虚拟串口看到启动信息、自检结果与心跳，
  以便在不接额外串口硬件的情况下观察系统状态。
- **优先级**：`should`
- **类别**：`interface`
- **EARS 验收**：
  - THE SYSTEM SHALL 将 SCIA 配置为 115200-8N1（TX = GPIO29，RX = GPIO28，LSPCLK 30MHz）。
  - WHEN 上电完成，THE SYSTEM SHALL 打印 banner：目标 MPN、板型、固件版本、SYSCLK（Hz）、构建时间。
  - THE SYSTEM SHALL 每 5 秒打印一行心跳 `alive, steps=<count>, mode=<mode>`。
  - THE SYSTEM SHALL 仅使用带长度边界的格式化函数输出数字，禁止使用 `sprintf` 写入定长缓冲。
- **可执行验收**：
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser` 成功且 `fmt.c`、`sci_log.c` 链接
  - 上板：串口工具（115200-8N1）收到 banner、`self-test … OK`、周期心跳

## 4. 明确不做（Out of Scope）

- 不提供按键/外部输入切换模式（模式由编译期常量决定）。
- 不做 PWM 调光（呼吸灯），仅做数字电平的亮/灭与节拍。
- 不引入 RTOS（见 ADR 0001 的 FreeRTOS `portasm.asm` 工具链不兼容说明）。
- 不做 CAN/FSI/ADC 等其它外设驱动。

## 5. 未决项（Open Questions）

| # | 问题 | 影响的需求 | 结论 |
|---|---|---|---|
| 1 | 是否需要按键切换 pattern 模式？ | REQ-001 | 暂不需要；如需按键必须新开 spec（M 级变更） |
| 2 | 是否要用外部 20MHz 晶振提升时钟精度？ | REQ-003 | 暂用 INTOSC2；若上层需求出现精确波特率/计量则另立 spec（见 ADR 0001） |
| 3 | `200ms ±2%` 由谁裁决？ | REQ-003 | 只能由外部时基仪器裁决：板上 JTAG peek 的采样间隔是 20ms，量化误差远大于 2%（4ms），因此 ±2% 不能由 HIL 断言。2026-10-18 用户决定仪器暂不接入 → 当前门禁只判 ±25%，±2% 保持未裁决（不得记为 verified）。接入逻辑分析仪/示波器后在本行改为已裁决并补记录 |

---

### 检查清单

- [x] 每条 REQ 都有用户故事 + EARS 验收 + 可执行验收
- [x] EARS 句可观测、可判定
- [x] 已列明 Out of Scope 与未决项
