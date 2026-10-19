# design.md — LED 流水灯（led-chaser）

> 状态：`design-approved` ｜ 上游：`requirements.md`（REQ-001…REQ-005）

## 1. 概述

分三层：**纯逻辑层**（pattern 引擎 + 数字格式化）→ **硬件适配层**（GPIO/时钟/SCI）→ **应用层**（main、
上电自检、节拍循环）。纯逻辑层只依赖 `<stdint.h>/<stdbool.h>/<stddef.h>`，因而可被主机侧单测
完整覆盖；目标侧上电自检复用**同一组参考常量**，保证"主机验证"与"板上行为"不是两套真相。

## 2. 架构与分层

```
main.c
  ├─ board_clock_init()         硬件：INTOSC2 → PLL → 120MHz + 校验
  ├─ board_gpio_init()  ← led_hw.c（依赖 driverlib gpio.h + board_pins.h）
  ├─ sci_log_init()/puts()       硬件：SCIA 轮询 → XDS110 虚拟 COM
  └─ self_test_run()            纯逻辑参考序列校验 + fmt 参考向量校验
        ↓ （通过 → 正常形态 / 失败 → fail-safe）
     led_pattern_mask()         纯逻辑：模式 + 通道数 + 节拍 → 掩码
     led_pattern_levels()       纯逻辑：掩码 + 极性 → 各位电平
        ↓
     led_hw_apply()            硬件：driverlib GPIO_writePin 写两颗 LED

fmt_u32_dec / fmt_u32_hex      纯逻辑：带边界的数字格式化（禁止 sprintf）
```

符合 AGENTS.md §4 与 `harness/docs/30-编码与单测规范.md`：纯逻辑模块不 include driverlib 头文件。

## 3. 组件与接口

| 组件 | 文件 | 职责 | 平台相关 |
|---|---|---|---|
| `led_pattern` | `source/led_pattern.c/.h` | 流水图样 + 极性映射 + 参考常量 | 否 |
| `fmt` | `source/fmt.c/.h` | 边界安全的十进制/十六进制格式化 | 否 |
| `board_pins` | `source/board_pins.h` | LED/SCI 引脚唯一定义点 | 头文件常量 |
| `board_clock` | `source/board_clock.c/.h` | 时钟初始化与 120MHz 校验 | 是 |
| `led_hw` | `source/led_hw.c/.h` | GPIO 初始化 + 按掩码写电平 | 是 |
| `sci_log` | `source/sci_log.c/.h` | SCIA 初始化 + 轮询发送 | 是 |
| `self_test` | `source/self_test.c/.h` | 上电自检 + fail-safe 判定 | 混合（断言用纯逻辑） |
| `main` | `source/main.c` | 启动、自检、节拍循环、心跳 | 是 |

### 接口契约

```c
/* source/led_pattern.h —— 纯逻辑，主机/目标共用 */
#define LED_PATTERN_MAX_CHANNELS 32U
#define LED_PATTERN_REF_FNV1A    0x<see header>U   /* 参考校验值：CHASE/BOUNCE/BLINK 前 8 拍序列的 FNV-1a */

typedef enum {
    LED_PATTERN_MODE_CHASE  = 0U,   /* 单向循环 */
    LED_PATTERN_MODE_BOUNCE = 1U,   /* 折返 */
    LED_PATTERN_MODE_BLINK  = 2U,   /* 全亮/全灭交替 */
    LED_PATTERN_MODE_COUNT
} led_pattern_mode_t;

/* 注：C28x 无 8 位类型（char 为 16 位），channel_count 用 uint16_t 承载 */
uint32_t led_pattern_mask(led_pattern_mode_t mode, uint16_t channel_count, uint32_t step);
uint32_t led_pattern_levels(uint32_t mask, uint16_t channel_count, bool active_low);
uint32_t led_pattern_reference_fnv1a(uint16_t channel_count);   /* 目标侧自检用 */

/* source/fmt.h —— 纯逻辑 */
int32_t fmt_u32_dec(uint32_t value, char *dst, uint32_t dst_size);   /* 返回写入字符数（-1 = 缓冲不足） */
int32_t fmt_u32_hex(uint32_t value, char *dst, uint32_t dst_size);   /* "0xXXXXXXXX" 形式 */
```

- `led_pattern_mask()`：非法 `channel_count`（0 或 >32）返回 0，无 UB。
- `led_pattern_levels()`：仅对低 `channel_count` 位做极性取反，未使用位返回 0。
- `fmt_*()`：至少写入终止符；缓冲不足返回 -1 且不越界写。

## 4. 数据与内存

- pattern 引擎**无静态状态**（全部由入参推导）→ 天然可重入、天然可测。
- RAM：仅栈 + `sci_log` 的 64 字节临时缓冲 ≈ < 200 字节新增。
- Flash：预计 `.text` < 8KB（384KB budget 的 ~2%）；`ccs.ps1 size` 提供实测值。
- 无动态内存（符合 AGENTS.md §4）。

## 5. 时序与并发

- 节拍源：**CPU Timer0**，128-bit? → 32-bit 计数，周期 = `step_ms × (SYSCLK/1000)`；
  200ms @120MHz = 24,000,000 counts（远小于 2^32，无溢出风险）。
- 主循环轮询溢出标志 → 清标志 → `step++`；**不使用中断**（降低首版复杂度，见 EARS REQ-003 允许计时源为 CPU Timer）。
- 时钟校验：`SysCtl_getClock(DEVICE_OSCSRC_FREQ)` 与 120MHz 比对，失败进 fail-safe（REQ-003 异常分支）。
- LSPCLK 显式设为 SYSCLK/4 = 30MHz，使 SCI 波特率计算确定。

## 6. 错误处理

| 场景 | 处理策略 | 表现 |
|---|---|---|
| 时钟未到 120MHz | fail-safe | 双灯 100ms 同拍闪烁 + 日志 `clock FAIL` |
| pattern 自检失败 | fail-safe | 双灯快闪 + 日志 `self-test FAIL (code=N)`，N 存全局供调试器读 |
| fmt 自检失败 | fail-safe | 同上，code 区分 |
| `fmt_*()` 缓冲不足 | 返回 -1，不写越界 | 调用方打印占位 `<?>` |
| LED4/LED5 引脚被改动 | 由 `board_pins.h` 单一真相 + 单测断言参考序列守住 | 主机单测先失败 |

## 7. 验证策略

- **主机侧单测**（`harness/tests/host/test/`）：
  - `test_led_pattern.c`：CHASE/BOUNCE/BLINK 三种模式前 8 拍逐项断言、回绕与折返、
    非法通道数返回 0、极性映射（active-low / active-high）、参考 FNV-1a 常量自证。
  - `test_fmt.c`：`fmt_u32_dec` 的 0 / 边界 / UINT32_MAX，缓冲不足返回 -1 且不越界；
    `fmt_u32_hex` 输出格式校验。
- **目标侧**：上电自检 + `harness/tests/on-target/README.md` 清单（极性/节拍/心跳/故障注入）。
- **构建**：`ccs.ps1 build` 双配置（CPU1_FLASH / CPU1_RAM）+ `ccs.ps1 size`。

## 8. 需求覆盖表

| 需求 | 设计落点 | 验证手段 |
|---|---|---|
| REQ-001 | §3 `led_pattern_mask()`、§5 无状态 | `test_led_pattern.c` 序列/回绕/边界用例 + `ccs.ps1 build` |
| REQ-002 | §3 `led_pattern_levels()`、`board_pins.h` 单点定义 | `test_led_pattern.c` 极性用例 + 上板极性测量 |
| REQ-003 | §5 节拍源 CPU Timer0 + 时钟校验 | 上板逻辑分析仪 200ms ±2% + 构建通过 |
| REQ-004 | §6 错误表、`self_test.c` 共享参考常量 | 主机单测参考向量 + 上板 `self-test FAIL` 故障注入 |
| REQ-005 | §3 `sci_log` / `fmt`、LSPCLK 30MHz | 上板串口 banner/心跳 + `test_fmt.c` |

## 9. 风险与取舍

| 风险/取舍 | 影响 | 缓解 |
|---|---|---|
| 轮询节拍不如中断精确/低功耗 | CPU 空转、节拍受主循环负载影响 | 首版取舍：主循环无其它负载，节拍由硬件 Timer 溢出给出，不依赖软件延时；后续如需低功耗再立 spec 切中断 + IDLE |
| 自检参考值与实现同源，可能"自证循环" | 参考值随实现一起改坏 | 主机单测额外硬编码逐拍掩码表（不依赖参考值），两套校验互为独立 |
| INTOSC2 精度低于外部晶振（±1–2%） | SCI 波特率/节拍轻微偏差 | 已在 115200 容错内；如后续需要精度，按未决项 2 立 spec |

---

### 检查清单

- [x] 架构分层符合铁律
- [x] 接口契约明确
- [x] 内存/Flash 预算有估算
- [x] 需求覆盖表覆盖全部 REQ
