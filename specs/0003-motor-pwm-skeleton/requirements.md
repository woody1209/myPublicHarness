# requirements.md — 电机 PWM 骨架（无电机）

> 状态：`done`（三件套定稿；状态机含 requirements-approved/design-approved/implementing/done） ｜ 关联 ADR：`0001-平台与目标选型.md`
> 机读镜像：`spec.json`
> 目标工程：`projects/launchxl-f280039c-motor-pwm`

## 1. 简介

在 **LAUNCHXL-F280039C** 上实现电机 PWM 骨架：ePWM1/2/3 输出**三相互补 + 死区**
的 SVPWM 波形（10 kHz 中心对齐），由**开环电角度**积分器驱动 min-max 定心的
SVPWM 调制器；ADC 在 PWM 周期经 SOCA 触发采样一相电流采样引脚（A2）；
CMPSS1 硬件比较器经 XBAR→ePWM Trip Zone 形成**一次性锁存**的过流保护链，
动作时六路 PWM 全关；SCI 115200 打点输出全部关键量；上电自检覆盖纯逻辑与
板级链路。**本 spec 明确不接电机、不接功率板**——目标是"看得见的算法与 PWM
控制结果"，而非电机真转。

## 2. 术语表

| 术语 | 含义 |
|---|---|
| A/B/C 相 | 三路上桥/下桥互补输出：GPIO0/1（A 相，ePWM1A/B）、GPIO2/3（B 相，ePWM2A/B）、GPIO4/5（C 相，ePWM3A/B） |
| 中心对齐 | up-down 计数模式：载波从 0 升到 PRD 再降回 0，频率 = SYSCLK/(2·PRD) |
| 死区（DB） | 互补边沿间的插入延时，防止上下桥直通；RED=上升沿延时，FED=下降沿延时 |
| 互补极性 | POLSEL=2：OUTB = ¬OUTA（TI epwm_ex8 "Active High Complementary" 实证） |
| SVPWM | 电压空间矢量调制：α/β → 三相占空比，min-max 定心注入零序分量 |
| 过调制守卫 | max−min > 1 时整体缩放，保证线性区可表达（不追求过调制能力） |
| 电角度 | 开环积分器输出的 0..359.9°（内部以 0.1° 为分辨率，0..3599） |
| ISENSE | 电流采样引脚 A2（pin17）：ADCA 通道 2 与 CMPSS1 正端（CMPIN1P）物理同脚 |
| CMPSS→TZ 链 | CMPSS1 比较器 → XBAR TRIP4 → ePWM DCBH → DCBEVT1 → TZA/TZB=LOW 一次性锁存 |
| 一次性锁存 | DCBEVT1 事件锁存后不清源不恢复，须软件显式清 TZ（保护链语义） |
| fail-safe | TZ 锁存/自检失败后的安全形态：六路 PWM 硬件保持 LOW，固件打印 FAULT 并停跑 |
| COMPSTS | CMPSS 比较器状态位（只读），用于自检时双向探测而不触发 Trip |
| CMPA 32 位寄存器对 | ePWM 比较值 `CMPA:CMPAHR`@字 0x6A/0x6B（C28x 字地址），**有效占空比在高字 0x6B**（driverlib 写 `HWREGH(base+EPWM_O_CMPA+1)`）；TBPRD 同理有效半字 0x63。HIL/观测读低字（0x6A=CMPAHR 恒 0）会误判"占空比未写入" |
| `board_cmpss_wait_status(expect)` | 返回 **true=当前 COMPSTS 匹配 expect**，**不是状态值**；probe 判定用 `hi_ok && lo_ok`（两方向均达期望），打印行的 `hi=/lo=` 才是比较器实际状态 |

## 3. 需求

### REQ-001 板级引脚绑定与唯一真相

- **用户故事**：作为板级开发者，我想要所有引脚绑定只在 `board_pins.h` 出现一次，
  以便换板或改绑时不会出现"多个真相"，且 JTAG 引脚永不被占用。
- **优先级**：`must`
- **类别**：`interface`
- **EARS 验收**：
  - THE SYSTEM SHALL 将 A/B/C 相绑定到 GPIO0/1、GPIO2/3、GPIO4/5（ePWM1/2/3 的 A/B 输出），ISENSE 绑定到 A2，LED4/LED5 维持 GPIO20/GPIO22，SCIA 维持 GPIO28/29。
  - THE SYSTEM SHALL 仅通过 `board_pins.h` 中的 `BOARD_*_PIN` / `BOARD_*_PINCFG` 宏引用上述绑定（单一真相）。
  - THE SYSTEM SHALL NOT 将 GPIO35–38 配置为 GPIO 功能（保护 cJTAG 调试口）。
  - WHEN 任意外设输出引脚完成 mux 配置，THE SYSTEM SHALL 同时把该引脚方向设为输出（`GPIO_DIR_MODE_OUT`），否则 GPxDIR=0 时驱动无波形。
- **可执行验收**：
  - `.\harness\tools\static-check.ps1 -Project launchxl-f280039c-motor-pwm`：PIN-JTAG-3538 / PIN-BARE-LITERAL / PIN-BARE-PINCFG / PIN-DUPLICATE-TRUTH 四条规则 0 命中
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm` 成功
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-002 ePWM 三相互补 + 死区（10 kHz 中心对齐）

- **用户故事**：作为控制工程师，我想要三相互补 PWM 以 10 kHz 中心对齐载波输出且带可预期的死区，
  以便示波器/逻辑分析仪可直接验证 SVPWM 波形形态，并为后续电流环提供确定性时基。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 配置 ePWM1/2/3 为 up-down 计数（`EPWM_COUNTER_MODE_UP_DOWN`）、PRD=6000（120 MHz SYSCLK → 10 kHz），三相同相（TBPHS=0）。
  - THE SYSTEM SHALL 配置 B 路为 A 路的互补输出（DBCTL POLSEL=2，`EPWM_setDeadBandDelayPolarity(RED, ACTIVE_HIGH)` + `(FED, ACTIVE_LOW)`）。
  - THE SYSTEM SHALL 配置 RED=FED=120 个 TBCLK 周期（1 µs @120 MHz）的死区（`EPWM_setDeadBandDelayMode(RED/FED, true)`，全周期计数）。
  - THE SYSTEM SHALL 采用标准 SVPWM 动作序列：ZRO→SET、UP_CMPA→CLEAR、DOWN_CMPA→SET，占空比 = CMPA/PRD。
  - THE SYSTEM SHALL 将占空比写入 CMPA 的**有效半字**（C28x 32 位寄存器对 `CMPA:CMPAHR`@字 0x6A/0x6B，driverlib `EPWM_setCounterCompareValue` 写高字 0x6B；TBPRD 同理有效半字为 0x63）——读低字（0x6A=CMPAHR 恒 0）不得被用作"占空比是否写入"的证据。
  - THE SYSTEM SHALL 在配置完成后回读 TBPRD/DBCTL 寄存器并打印 `pwm: prd=6000 db=120 pol=2 OK`。
- **可执行验收**：
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm` 成功
  - HIL：DSS 回读 ePWM1/2/3 的 TBPRD/DBCTL/AQ 寄存器与声明一致；CMPA 占空比观测须读高字 0x6B（`hil_test_mp.js` H5）
  - 上板：示波器测得互补边沿间 1 µs 死区、载波 10 kHz（`harness/tests/on-target/README.md`）
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-003 ADC SOCA 电流采样（A2）

- **用户故事**：作为算法开发者，我想要每 PWM 周期在载波中点附近采样一次 ISENSE 原始码值，
  以便电流环（后续 spec）有确定性时基的输入，本 spec 先把采样链路与读数链路打通。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 配置 ADCA SOC0 由 ePWM1 SOCA 触发（`ADC_TRIGGER_EPWM1_SOCA`），通道为 A2（`ADC_CH_ADCIN2`）。
  - THE SYSTEM SHALL 在 ePWM 侧使能 SOCA 输出（`EPWM_setADCTriggerSource(EPWM1_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD)` + `EPWM_enableADCTrigger`），并配置 `EPWM_setADCTriggerEventPrescale(EPWM1_BASE, EPWM_SOC_A, 1U)`（ETPS.SOCAPRD=1，SOCAPRD2=1）；否则 SOC 静默失效。
  - WHEN 超级循环轮询到 SOC 完成标志，THE SYSTEM SHALL 读取 `ADC_readResult(ADCxRESULT_BASE, soc)` 得到 12 位原始码（0..4095）。
  - THE SYSTEM SHALL 在 `motor:` 打点行中附带该原始码（`adc=<raw>`）。
  - IF ADC 结果读取超时，THEN THE SYSTEM SHALL 以 0xFFFF 占位并继续运行（不阻塞主循环）。
  - **自检探针契约**：THE SYSTEM SHALL 在 `cmpss: adc=<baseline> hi=<0|1> lo=<0|1> OK|FAIL` 自检探针行中，以 `adc=` 字段打印双向探测所参考的 ADC 基线实测值（供证据日志可见，非摆设）；`hi=`/`lo=` 字段 SHALL 打印**比较器实际状态**（COMPSTS），而非"自检是否匹配期望"的布尔。
- **可执行验收**：
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm` 成功
  - HIL：SCI `motor:` 行中 `adc=` 值随 A2 引脚电压变化（悬空时码值稳定、不锁死满量程）
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-004 CMPSS→TZ 硬件保护链（一次性锁存）

- **用户故事**：作为安全工程师，我想要过流比较器经纯硬件路径关闭六路 PWM 且锁存不清，
  以便即使固件卡死，功率桥也不会在故障状态下继续收到驱动信号。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 配置 CMPSS1：正端 = CMPIN1P（与 A2 同脚，CMPHPMUX 复位默认即选中），负端 = 内部 DAC（shadow 模式），DAC 上电默认置 4095（最大阈值，浮置噪声不误触发）。
  - THE SYSTEM SHALL 打通链路：XBAR TRIP4 ← CMPSS1_CTRIPH（mux00）→ ePWM1/2/3 的 DCBH → DCBEVT1（`EPWM_TZ_EVENT_DCXH_HIGH`）→ `EPWM_enableTripZoneSignals(EPWM_TZ_SIGNAL_DCBEVT1)` → TZA/TZB 动作 = LOW。
  - WHEN DCBEVT1 事件发生，THE SYSTEM SHALL 保持六路 PWM 输出 LOW 直至软件显式清 TZ（一次性锁存，非周期模式）。
  - WHEN 固件检测到 TZ 锁存，THE SYSTEM SHALL 打印 `FAULT: tz-latched` 一行、停止电角度推进、置 LED4 慢闪故障指示。
  - THE SYSTEM SHALL 在 CMPSS 双向自检通过后才使能 TZ 链（防止自检期间的阈值穿越误触发）。
- **可执行验收**：
  - `.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm` 成功
  - HIL：DSS 写 CMPSS1 DAC 寄存器强制越限 → 回读 TZ 状态确认锁存、PWM 输出变 LOW；软件清 TZ 后恢复（`hil_test_mp.js` H5/H5b）
  - 正常上电 90 s SCI 日志无 `FAULT:` 行（fail-safe 不误触发）
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-005 开环电角度发生器（纯逻辑）

- **用户故事**：作为算法开发者，我想要一个不依赖硬件、可主机单测的电角度积分器，
  以便无电机也能确定性地验证角度速率、回绕与边界行为。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 提供 `angle_gen_advance(&state, tick_us)`：以 0.1° 为分辨率的定点积分器，`tick_us` 为距上次调用的微秒数。
  - WHEN 累计角度达到 359.9°，THE SYSTEM SHALL 回绕到 0.0°（模 3600）。
  - THE SYSTEM SHALL 提供 `angle_gen_set_rate_hz_x100(&state, rate)` 设置电频率（单位 0.01 Hz），非法值（>上限）时保持原速率不变。
  - THE SYSTEM SHALL 保证整数累加不丢步：相同累计时间拆分成的不同 tick 序列，最终角度一致（结合律）。
  - IF `tick_us` 为 0 或状态指针为空，THEN THE SYSTEM SHALL 原样返回当前角度且不修改状态。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：`test_angle_gen.c` 覆盖速率设置、回绕、结合律、空指针/零 tick 边界
  - `.\harness\tools\static-check.ps1`：`angle_gen.c` 不含 driverlib 头（纯逻辑分层铁律）
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-006 SVPWM 调制器（纯逻辑）

- **用户故事**：作为算法开发者，我想要 min-max 定心的 SVPWM 纯函数，
  以便在主机侧完整验证占空比数学正确性（线电压不变性、定心范围、过调制守卫），无需上板。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 提供 `svpwm_modulate(angle_x10, mag_permille, &duties)`：输入电角度（0.1° 分辨率）与幅度（千分比），输出三相占空比（千分比，0..1000）。
  - THE SYSTEM SHALL 采用 min-max 定心：`ta=α; tb=−0.5α+0.866β; tc=−0.5α−0.866β`，减最小值实现零序偏移。
  - THE SYSTEM SHALL 保证线电压不变性：`da−db = ta−tb`、`db−dc = tb−tc`（定心只改共模，不改差模）。
  - WHEN `mag_permille` 为 0，THE SYSTEM SHALL 输出三相 500‰（零矢量）。
  - WHEN max−min > 1000‰（过调制输入），THE SYSTEM SHALL 将三相整体按 (max−min)/1000 等比缩放后输出（守卫）。
  - IF `duties` 指针为空或 `mag_permille` > 1000，THEN THE SYSTEM SHALL 返回错误码且不写输出。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：`test_svpwm.c` 覆盖定心范围（0..1000）、线电压不变性、零幅度、过调制守卫、非法入参
  - `.\harness\tools\static-check.ps1`：`svpwm.c` 不含 driverlib 头（纯逻辑分层铁律）
- **设计锚点**：`design.md#3-组件与接口`（② 阶段回填）

### REQ-007 SCI 控制台打点契约

- **用户故事**：作为验证者，我想要固件通过 SCI 115200 打印确定的控制台契约行，
  以便 run-hil.ps1 的串口腿能用同一组字符串判定固件行为，无需示波器也能看到算法与 PWM 结果。
- **优先级**：`must`
- **类别**：`interface`
- **EARS 验收**：
  - THE SYSTEM SHALL 上电后依次打印：banner `=== LAUNCHXL-F280039C motor-pwm ===`、`sysclk : 120000000 Hz`。
  - THE SYSTEM SHALL 打印自检行：`pwm: prd=6000 db=120 pol=2 OK`、`cmpss: adc=<baseline> hi=1 lo=0 OK`、`tz: chain armed`、`self-test: svpwm/angle/fmt OK`。
  - THE SYSTEM SHALL 以约 10 Hz 周期打印 `motor: ang=<0..3599> mag=<0..1000> a=<0..1000> b=<0..1000> c=<0..1000> adc=<raw>`。
  - THE SYSTEM SHALL 以约 1 Hz 周期打印 `alive, ticks=<n>`。
  - THE SYSTEM SHALL 只使用整数格式化（`fmt_u32_dec` / `fmt_u32_hex`；本平台无浮点格式化）。
  - IF 自检任一子项失败，THEN THE SYSTEM SHALL 打印 `self-test: <sub> FAIL` 并进入 fail-safe 形态（REQ-008）。
- **可执行验收**：
  - `.\harness\tools\run-hil.ps1 -Project projects\launchxl-f280039c-motor-pwm`：serialChecks 九项（banner/sysclk/self-test/pwm/cmpss/tz/motor/heartbeat/no-spurious-fault）全命中
  - 打点格式与本 spec 的 EARS 句逐字一致
- **设计锚点**：`design.md#7-验证策略`（② 阶段回填）

### REQ-008 上电自检与 fail-safe 形态

- **用户故事**：作为验证者，我想要上电自检覆盖纯逻辑与板级链路，且任何失败都进入确定的安全形态，
  以便"看不见内部状态"的板子也能给出可信的 PASS/FAIL 证据，而不会半随机运行。
- **优先级**：`must`
- **类别**：`functional`
- **EARS 验收**：
  - THE SYSTEM SHALL 在启动顺序（ramfunc 重定位 → 禁看门狗 → PIE → 时钟 → 外设）完成后执行自检，自检顺序为：pwm 寄存器回读 → CMPSS 双向 COMPSTS 探测 → 使能 TZ 链 → svpwm/angle/fmt 纯逻辑参考序列。
  - WHEN CMPSS 双向探测，THE SYSTEM SHALL 以 ADC 实测 A2 电平为基准：DAC 置 **实测−512** 时期望 COMPSTS=1（CMPIN1P 高于参考即跳断）、置**满量程 `BOARD_CMPSS_DAC_MAX`（4095）** 时期望 COMPSTS=0（免跳方向用物理上界而非实测+512，规避 A2 悬空+ePWM 载波耦合噪声超 512 码窗口的误判）；探测后 DAC 恢复满量程。
  - WHEN 双向探测判定，THE SYSTEM SHALL 以 **`board_cmpss_wait_status()` 的匹配布尔（`hi_ok && lo_ok`，两方向均达期望）** 为准入条件；`board_cmpss_wait_status(expect)` 返回的是"状态是否匹配 expect"而非状态值本身，不得当作 COMPSTS 值误用。
  - IF 任一自检子项失败，THEN THE SYSTEM SHALL 打印失败行、保持六路 PWM LOW、双 LED 100 ms 同拍闪烁，并不再推进电角度。
  - WHEN TZ 链锁存，THE SYSTEM SHALL 打印 `FAULT: tz-latched`、停止角度推进、LED4 慢闪，直至软件清 TZ。
  - WHEN TZ 锁存清除后恢复判定，THE SYSTEM SHALL 以 **CMPA 寄存器（高字 0x6B）重新变化**作为"调制已恢复"的证据，而非瞬时 GPADAT_R 电平读数——中心对齐 up-down + 本 AQ 序列下，占空比=PRD 的相 A/B 整周期为低，瞬时读全零属正常波形而非未恢复。
- **可执行验收**：
  - `.\harness\tools\run-host-tests.ps1`：`self_test` 纯逻辑部分参考序列通过
  - HIL：DSS 读 `g_self_test_code==0`（H3）；故障注入后 `FAULT: tz-latched` 出现在 SCI 日志；TZ 清除后 CMPA 寄存器恢复变化（`hil_test_mp.js` H5-trip-resume）
- **设计锚点**：`design.md#6-错误处理`（② 阶段回填）

## 4. 明确不做（Out of Scope）

- **不接电机、不接功率板**：无电流环、无速度环、无位置环；开环角度仅供波形演示。
- **无编码器/Hall 输入**：eCAP 测速留待后续 spec。
- **不过调制 >2/3 线性区**：仅实现守卫，不追求过调制算法。
- **无中断驱动**：superloop 轮询 + 硬件 TZ 链（TZ 不依赖软件）；切中断属 M 级变更。
- **无 RTOS/CLA**：单核 C28x 裸机。
- **无浮点打点**：SCI 只用整数格式化（平台约束）。

## 5. 未决项（Open Questions）

| # | 问题 | 影响的需求 | 结论 |
|---|---|---|---|
| 1 | CMPSS DAC 上电安全值取 4095 还是动态测量值？ | REQ-004 | 已定：4095（复位即最大阈值，浮置不误触；自检后再武装） |
| 2 | 电频率/幅度默认值？ | REQ-005/006 | 已定：5.00 Hz（500 ×0.01Hz）、80%（800‰），编译期常量可调 |
| 3 | TZ 用一次性锁存还是周期清除？ | REQ-004 | 已定：DCBEVT1 一次性锁存（保护链语义；恢复须软件显式清） |
| 4 | A2 悬空时 ADC 读数不可预测，自检是否可能 DEGRADED？ | REQ-003/008 | 已定：跳断方向以实测−512 对任意基准成立；免跳方向改用满量程 DAC（物理上界）双向验证；仍不自洽则诚实 FAIL（不伪造 PASS） |

---

### 检查清单（进入 ② design 前自检）

- [x] 每条 REQ 都有用户故事 + ≥1 条 EARS 验收 + ≥1 条可执行验收
- [x] EARS 句全部可观测、可判定（无"尽量/较快"这类模糊词）
- [x] 已列明 Out of Scope 与未决项
- [x] 用户已确认（状态改为 `done`）
