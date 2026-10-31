# design.md — 电机 PWM 骨架（无电机）

> 状态：`done` ｜ 上游：`requirements.md`

## 1. 概述

方案：**开环电角度（定点积分器）→ SVPWM（min-max 定心纯函数）→ ePWM 三相互补+死区**；
ADC 经 ePWM1 SOCA 触发采样 A2；CMPSS1 比较器经 **XBAR→ePWM DCBH→DCBEVT1 一次性锁存**
关闭六路 PWM。全部算法放纯逻辑层（主机单测），全部寄存器操作放 board 层（上板验证）。
关键技术决策均已在 0001 基建期与本次调研期用 TI 官方例程/数据手册实证（见 §9 风险表）。

## 2. 架构与分层

```
main.c（启动顺序 ADR 0003：ramfunc relocate → 禁看门狗 → PIE → 时钟 → 外设 → 自检 → superloop）
  ↓ 调用
angle_gen.c / svpwm.c / fmt.c / self_test.c   ← 纯逻辑（只依赖 stdint/stdbool/stddef），主机单测
  ↓ 调用
board_epwm.c / board_adc.c / board_cmpss.c / sci_log.c  ← 硬件相关（driverlib / 寄存器）
```

分层铁律（AGENTS.md §4）：纯逻辑模块**不 include 任何 driverlib 头**，
`static-check.ps1` 的 LAY-DRIVERLIB-IN-LOGIC 规则强制；因此 svpwm/angle_gen
可整模块搬进主机 ctest。

## 3. 组件与接口

| 组件 | 文件 | 职责 | 平台相关 |
|---|---|---|---|
| `board_pins.h` | `source/board_pins.h` | 全部引脚绑定唯一真相（ePWM×6/A2/LED/SCIA） | 头文件宏 |
| `angle_gen` | `source/angle_gen.c/.h` | 开环电角度定点积分器 | 否 |
| `svpwm` | `source/svpwm.c/.h` | min-max 定心 SVPWM 调制器 | 否 |
| `fmt` | `source/fmt.c/.h` | 整数格式化（从 led-chaser 复制，无改动） | 否 |
| `self_test` | `source/self_test.c/.h` | 纯逻辑参考序列 + 板级链路自检编排 | 否/是（板级部分调 board_*） |
| `board_epwm` | `source/board_epwm.c/.h` | ePWM1/2/3 互补+死区配置、占空比写入、寄存器回读 | 是 |
| `board_adc` | `source/board_adc.c/.h` | ADCA SOC0←EPWM1 SOCA、A2 通道、结果轮询 | 是 |
| `board_cmpss` | `source/board_cmpss.c/.h` | CMPSS1 配置、XBAR/TZ 链武装、DAC 阈值、COMPSTS 读取 | 是 |
| `sci_log` | `source/sci_log.c/.h` | SCIA 115200-8N1 轮询发送（从 led-chaser 复制） | 是 |
| `main` | `source/main.c` | 启动顺序、自检编排、superloop 打点 | 是 |

### 接口契约

```c
/* angle_gen.h — 纯逻辑 */
typedef struct { uint32_t angle_x10; uint32_t rate_hz_x100; uint64_t accum; } angle_gen_t; /* 0..3599, 0.01Hz */
void     angle_gen_init(angle_gen_t *g, uint32_t rate_hz_x100);
uint32_t angle_gen_advance(angle_gen_t *g, uint32_t tick_us);      /* 返回 0..3599 */
int      angle_gen_set_rate_hz_x100(angle_gen_t *g, uint32_t rate); /* 0=ok, <0=非法 */

/* svpwm.h — 纯逻辑；单位：千分比（0..1000） */
typedef struct { uint16_t a, b, c; } svpwm_duties_t;
int svpwm_modulate(uint16_t angle_x10, uint16_t mag_permille, svpwm_duties_t *out); /* 0=ok, <0=非法入参 */

/* board_epwm.h */
void     board_epwm_init(void);                       /* 三相互补+死区，TBCLKSYNC 先禁后使 */
void     board_epwm_set_duties(uint16_t a, uint16_t b, uint16_t c); /* 千分比 → CMPA */
bool     board_epwm_verify(void);                     /* 回读 TBPRD/DBRED/DBFED/DBCTL 全部模块 */
void     board_epwm_report(uint16_t *prd, uint16_t *db, uint16_t *pol); /* ePWM1 回读值（打点用） */

/* board_adc.h */
void     board_adc_init(void);                    /* SOC0 ← EPWM1 SOCA, ch=A2 */
uint16_t board_adc_read_latest(void);             /* 0..4095；无新数据返回上次值 */
uint16_t board_adc_read_raw_blocking(void);       /* 自检用：触发一次转换并等待；超时返回 0xFFFF */

/* board_cmpss.h */
void     board_cmpss_init(void);                  /* CMPSS1: 正端=A2, 负端=DAC(4095) */
void     board_cmpss_arm_tz(void);                /* XBAR TRIP4 + DCBH + DCBEVT1 + TZA/B=LOW */
void     board_cmpss_set_dac(uint16_t value);     /* 0..4095 */
uint16_t board_cmpss_get_dac(void);
bool     board_cmpss_comp_status(void);           /* COMPSTS 只读（比较器实际状态） */
bool     board_cmpss_wait_status(bool expect, uint16_t timeout_ms); /* true=状态匹配 expect（非状态值！） */
void     board_cmpss_clear_filter_latch(void);    /* 探测间隙清滤波锁存 */
bool     board_cmpss_tz_latched(void);            /* TZ 是否锁存 */
void     board_cmpss_clear_tz(void);              /* 软件清 TZ */
```

- 返回码语义：纯逻辑 `0=ok / <0=非法入参`；board 层 `void`（失败由自检层捕获，见 §6）。
- 调用方契约：`board_epwm_set_duties()` 只写 CMPA，不改 AQ/TZ 配置；
  `board_cmpss_arm_tz()` 必须在 CMPSS 双向自检通过后调用（REQ-004）。
- **CMPSS probe 判定契约（2026-10-25 上板实证）**：`board_cmpss_wait_status(expect)`
  返回的是"COMPSTS 是否匹配 expect"的布尔，**不是状态值**。probe 判定必须
  `hi_ok && lo_ok`（跳断方向与免跳方向都达期望）；打印行的 `hi=`/`lo=` 才是
  `board_cmpss_comp_status()` 的**比较器实际状态**（trip 方向=1、免跳方向=0）。
  免跳方向 DAC 参考用**满量程 `BOARD_CMPSS_DAC_MAX`(4095)** 而非 `实测+512`：
  A2 悬空 + ePWM 载波耦合噪声幅度常超 512 码窗口，满量程是物理上界、方向确定。

### ePWM 关键配置（实证值）

- 计数模式 `EPWM_COUNTER_MODE_UP_DOWN`（=2），PRD=6000 → f = 120MHz/(2×6000) = 10 kHz，三相同相 TBPHS=0。
- DBCTL 布局（位域）：OUT_MODE[1:0]@0 / POLSEL[1:0]@2 / IN_MODE[1:0]@4。
  `EPWM_setDeadBandDelayMode(RED/FED, true)` 置 OUT_MODE；POLSEL=2 由
  `EPWM_setDeadBandDelayPolarity(RED, ACTIVE_HIGH)` + `(FED, ACTIVE_LOW)` 得到
  （TI epwm_ex8 "Active High Complementary" 实证：OUTB = ¬OUTA）。
  IN_MODE 复位默认 0（RED/FED 均取 ePWMA，无需另配）。
- 死区 120 cycles（1 µs @120 MHz），`EPWM_DB_COUNTER_CLOCK_FULL_CYCLE`。
- AQ 标准 SVPWM 序列：ZRO→SET(HIGH)、UP_CMPA→CLEAR(LOW)、DOWN_CMPA→SET(HIGH)；占空比 = CMPA/PRD。
- **CMPA/TBPRD 是 32 位寄存器对（C28x 字地址）**：`CMPA:CMPAHR`@字 0x6A/0x6B、
  `TBPRD:TBPRDHR`@字 0x62/0x63；driverlib `EPWM_setCounterCompareValue` 写
  `HWREGH(base+EPWM_O_CMPA+1)` = **高字 0x6B** 才是有效占空比（epwm.h:2649 实证）。
  HIL/观测读低字 0x6A 得到的是 CMPAHR（恒 0），会误判"CMPA 不变"（2026-10-25 假 FAIL 根因）。
  TBPRD 同理读高字 0x63。
- 时钟门控：先 `SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1..3)`（ADR 0005），
  TBCLKSYNC 先禁后使，最后 GPIO mux + `GPIO_setDirectionMode(OUT)`（REQ-001）。
- ADC 触发源 `ADC_TRIGGER_EPWM1_SOCA`（=5）；通道 `ADC_CH_ADCIN2`（A2）。
  **ePWM 侧也必须使能**：`EPWM_setADCTriggerSource(EPWM1_BASE, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD)`
  + `EPWM_enableADCTrigger(EPWM1_BASE, EPWM_SOC_A)` + **`EPWM_setADCTriggerEventPrescale(EPWM1_BASE, EPWM_SOC_A, 1U)`**
  （ETPS.SOCAPRD=1 / SOCAPRD2=1，2026-10-25 上板实证必修项；在 board_adc_init 完成）——
  只配 ADC 侧触发源而不开 ePWM SOCA 输出或不设 SOCAPRD，SOC 永远不触发（静默失效）。
- ADC 中断标志链：`ADC_setInterruptSource(ADCA, ADC_INT_NUMBER1, ADC_SOC_NUMBER0)`
  + **`ADC_enableInterrupt(ADCA, ADC_INT_NUMBER1)`**（INT1E，2026-10-25 上板实证的必修项）：
  F28003x 的 ADCINTFLG 位只在 INT1E=1 时才会置起——缺它时转换照常完成（RESULT0 更新）
  但标志永不置位，任何 EOC 轮询必然超时（曾致启动探针 adc=0xFFFF）。
  INT1E 不是 PIE 中断使能：本固件从不 EINT、IER 保持 0，脉冲只落在 PIE IFR，无向量风险。
- CMPSS→TZ 链（TI 官方 cmpss_ex1_asynch.c 范本）：
  CMPSS1 正端默认即 CMPIN1P（与 A2 共脚，CMPHPMUX 复位值 0 已选中，**无需 ASysCtl 调用**）；
  负端=内部 DAC（shadow）；`CMPSS_configDAC(CMPSS1_BASE, CMPSS_DACSRC_SHDW)`（config=0）
  即 SWLOADSEL=0=SYSCLK 自动加载，**本就正确**，勿 OR `CMPSS_DACVAL_SYSCLK`（=0x0000，OR 无效）；
  `XBAR_setEPWMMuxConfig(XBAR_TRIP4, XBAR_EPWM_MUX00_CMPSS1_CTRIPH)`
  + `XBAR_enableEPWMMux(XBAR_TRIP4, XBAR_MUX00)`；ePWM 侧
  `EPWM_enableDigitalCompareTripCombinationInput(base, EPWM_DC_COMBINATIONAL_TRIPIN4, EPWM_DC_TYPE_DCBH)`
  → `EPWM_setTripZoneDigitalCompareEventCondition(base, EPWM_TZ_DC_OUTPUT_B1, EPWM_TZ_EVENT_DCXH_HIGH)`
  → `EPWM_enableTripZoneSignals(base, EPWM_TZ_SIGNAL_DCBEVT1)`（一次性锁存）
  → `EPWM_setTripZoneAction(TZA/TZB, EPWM_TZ_ACTION_LOW)`。
  ePWM1/2/3 三片同样配置。无 `EPWM_selectTripZoneInput` API；TZSEL 经
  `EPWM_enableTripZoneSignals` 的 DCBEVT1 宏置位。

### SVPWM 算法（定点，千分比）

```
α   = mag · cos(θ) / 1000        （查表，θ 为 angle_x10×0.1°）
β   = mag · sin(θ) / 1000
ta  = α
tb  = −0.5·α + 0.866·β
tc  = −0.5·α − 0.866·β
off = min(ta, tb, tc)
da/db/dc = (ta−off), (tb−off), (tc−off)      /* min-max 定心，零序偏移 */
if (max−min > 1000) 整体 /= (max−min)/1000    /* 过调制守卫 */
if (mag == 0) da=db=dc=500                    /* 零矢量：三相 50% 定心 */
```

- 线性区 |V| ≤ 2/3（mag ≤ 666‰）时 max−min ≤ 1000 自动满足；mag=800‰ 默认值触发守卫路径，
  恰好把守卫逻辑纳入每次上板的常规执行路径（不是死代码）。
- 不变量：`da−db = ta−tb`（线电压不变性）；镜像对称 `f(3600−θ) = (a, c, b)`——主机单测直接断言。
- sin/cos 用**四分之一波定点查表**：`svpwm_table.h` 901 项（0..90°，每 0.1° 一项，int16 千分比，
  共 1802 B .const），全波靠象限反射导出（cos(x) = sin(x+90°)）；表由
  `projects/launchxl-f280039c-motor-pwm/tools/gen_svpwm_table.py` 生成，禁止手抄；
  无浮点运算、无运行时 math 库。

### angle_gen 算法

- 累加器 `angle_x10`（0..3599，0.1° 分辨率）；一个 0.1° 步对应
  `1e8 / (rate_hz_x100 × 3600)` µs 的 elapsed time。
- 实现取**精确有理累加**（64 位余数）：
  `accum += rate_x100 × tick_us × 3600; steps = accum / 100000000; accum %= 100000000;`
  再 `angle_x10 = (angle_x10 + steps) mod 3600`（O(1) 单次除法，无循环）。
  任意 tick 拆分序列终点一致（结合律可单测）。
- 默认 5.00 Hz（rate=500）、上限 500 Hz（rate=50000）；`tick_us=0` 或空指针原样返回。

### SCI 打点契约（与 REQ-007 逐字一致）

```
=== LAUNCHXL-F280039C motor-pwm ===
target : TMS320F280039C / LAUNCHXL-F280039C
version: <APP_VERSION> (<date> <time>)
note   : no motor attached - open-loop angle/SVPWM demo
sysclk : 120000000 Hz
rate   : 500 x0.01 Hz
mag    : 800 permille
pwm: prd=6000 db=120 pol=2 OK
cmpss: adc=<0..4095> hi=1 lo=0 OK
tz: chain armed
self-test: svpwm/angle/fmt OK
motor: ang=<0..3599> mag=800 a=<0..1000> b=<0..1000> c=<0..1000> adc=<raw>
alive, ticks=<n>
FAULT: tz-latched            （仅 TZ 锁存时出现一次）
tz: cleared                  （dwell 5 s 且比较器恢复后出现一次）
```

- `motor:` 行 10 Hz 一次（超级循环计时，CPU Timer 时基同 led-chaser）；`alive,` 1 Hz 一次。
- 全部整数（fmt 无浮点）：ang 单位 0.1°，mag/a/b/c 单位 ‰，adc 为 12 位原始码。
- banner 六行（target/version/note/sysclk/rate/mag）由 main 启动段打印；
  `cmpss: adc=<baseline> hi=<0|1> lo=<0|1>` 的 adc 字段是双向探测所参考的 ADC 基线，
  hi/lo 打印的是**比较器状态**（DAC=基线−512 期望 hi=1，DAC=基线+512 期望 lo=0），
  不是"自检是否见过跳断"——REQ-007 契约与 run-hil serialChecks 均按此语义。
  adc 字段的存在是为了让错误基线（如参考未稳前的陈旧码）在证据日志里可见，
  而不是静默地产出 hi=0 lo=0（2026-10-25 真机教训，见 §9）。

## 4. 数据与内存

- 静态数据：angle_gen 状态 16 B（C28x 上 uint32×2 + uint64×1 对齐后 8 字）；
  svpwm 四分之一波表 901 项 int16 = 1802 B（.const，flash）；
  无堆、无动态内存（MEM-DYNAMIC 规则强制）。
- 实测（`ccs.ps1 size`，CPU1_FLASH，2026-10-25 收口值）：.text 7457 字（命令文件分片
  .text.1=4096 + .text.2=3361，求和口径见 ADR 0009）、.const 2716 字（含 sin 表与常量池）、
  .cinit 28 + .TI.ramfunc 4 + codestart 2；FLASH 合计 10207 字 = 5.19%（128K 字）；
  RAM 合计 1040 字 = 2.94%（.args 512 + .stack 512 + .data 14 + .bss 2）。
  size-delta 腿按 state.json 基线守 .text ≤ +1024 字。
- 估算被实测取代后仍留档：波表从初稿"3600×2 B×2 = 14400 B"缩到 1802 B，
  是 §9 风险行"表占 flash"的实际结余来源；相对 T-012 首版基线（7274 字）+183 字
  来自 INT1E/SOCAPRD/CMPSS probe 契约与调试打印清理的净增量。

## 5. 时序与并发

- 执行上下文：**单超级循环**（无中断驱动；TZ 是硬件链，不依赖软件响应）。
- 循环体：轮询 ADC 新数据 → 推进电角度 → SVPWM → 写 CMPA → 到打点周期打印。
- 循环周期目标 < 200 µs（10 Hz 打点仅需 100 ms 计时精度，余量充足）。
- 可重入性：全部状态在 main 的静态结构内，无重入路径。

## 6. 错误处理

| 场景 | 处理策略 | 返回/表现 |
|---|---|---|
| `svpwm_modulate` 空指针 / mag>1000 | 不写输出，返回负码 | 调用方忽略该次更新（保持上次占空比） |
| ADC 无新数据 | 返回上次值（初值 0） | 打点继续，不阻塞 |
| CMPSS 双向探测 hi/lo 状态不符 | 该子项 FAIL → 不武装 TZ 链 → fail-safe | `cmpss: adc=<baseline> hi=X lo=Y FAIL` + 双灯 100 ms 同闪 |
| ePWM 寄存器回读不符 | 该子项 FAIL → 不启动 SVPWM → fail-safe | `pwm: prd=X db=Y pol=Z FAIL` + 双灯同闪 |
| 纯逻辑自检不符 | FAIL → fail-safe（不武装 TZ、不进 superloop） | `self-test FAIL` + `  code=<n>` |
| TZ 锁存（运行中） | 硬件已关六路；固件打印 FAULT、停角度、LED4 慢闪 | `FAULT: tz-latched`；dwell 5 s 且比较器回低于参考后 `tz: cleared` 恢复 |
| TZ 清除后"调制已恢复"判定 | 以 **CMPA 寄存器（高字 0x6B）重新变化**为证据，不读瞬时 GPADAT_R（中心对齐 + 本 AQ 下占空比=PRD 的相整周期为低，瞬时全零属正常波形） | H5-trip-resume 据 CMPA 变化判定 PASS/FAIL |
| TW 恢复 1-tick 竞态 | recovery 后固件 `continue` 跳过本 tick motor 块，PWM 恢复延迟 1 tick（10 ms）；HIL 分两阶段：先等 `tzAllClear()`，再判 CMPA 变化，勿同时要求"tz 清"且"PWM 非零" | H5b 两阶段判定 |
| A2 悬空/读数异常使双向探测无法自洽 | 诚实报 FAIL（不伪造 PASS，ADR 0009 精神）；免跳方向用满量程 DAC 规避噪声窗口 | `cmpss: adc=<baseline> hi=X lo=Y FAIL` |
| ADC 基线读取失败（上板实证：ADCINTFLG 在 INT1E=0 时永不置位，轮询必超时） | `board_adc_init` 必须调 `ADC_enableInterrupt`（INT1E 是 ADCINTFLG 的门，不是 PIE 中断使能；F28003x wrapper 实证 2026-10-25）；阻塞读先清 flag 再强制转换，最多 4 次重试 | 探针拿到 settled 基线；仍失败则 adc=0xFFFF 如实打印 |
| DSS halt 后立即读外设报 Error 0x20000 | 时钟门/未 init；需 `runAsynch` 让固件跑完再读 | HIL 脚本按此顺序 |

fail-safe 形态（REQ-008）：六路 PWM LOW（TZ 未武装时由 CMPA=PRD/2 全零矢量 + AQ 不动保持）、
双 LED 100 ms 同拍闪烁（复用 led-chaser 已验证形态）、不再推进电角度。

## 7. 验证策略

- **主机侧单测**（`harness/tests/host/`，modules.json v2 motor-pwm 条目）：
  - `test_angle_gen.c`：速率设置/非法值、回绕模 3600、结合律（同一总时长不同 tick 拆分）、
    空指针/零 tick、角度范围不变量。
  - `test_svpwm.c`：θ=0/90/180/270 的四象限占空比手算值；线电压不变性 `da−db=ta−tb`；
    零幅度三相 500‰；过调制守卫（mag=1000 时缩放后范围≤1000）；非法入参返回负码。
  - `test_mp_selftest.c`：`self_test` 纯逻辑参考序列（共享常量，单测与目标自检同一份）。
- **静态门**：`static-check.ps1 -Project launchxl-f280039c-motor-pwm`
  （引脚四规则 + 纯逻辑无 driverlib + 无 sprintf/动态内存）。
- **目标侧**：
  - `ccs.ps1 build`（FLASH+RAM 双配置）+ `size`（.text 跨段求和已修，ADR 0009）。
  - `run-hil.ps1 -Project projects\launchxl-f280039c-motor-pwm`：
    DSS 腿 H1 JTAG / H2 flash / H3 `g_self_test_code==0` / H4 PC 在主循环 /
    H5 ePWM+TZ+CMPSS 寄存器回读与强制越限实验（CMPA 读高字 0x6B；H5-trip-resume 判 CMPA
    变化而非瞬时 GPADAT_R；H5b 两阶段恢复判定）/ H6 独立运行；
    SCI 腿九项 serialChecks（含 no-spurious-fault 负向检查）。
  - `harness/tests/on-target/README.md` 增加 motor-pwm 手工清单（示波器死区/载波、
    DAC 强制越限观测 PWM 全关）。

## 8. 需求覆盖表

| 需求 | 设计落点 | 验证手段 |
|---|---|---|
| REQ-001 | §3 board_pins.h 单一真相；GPIO 方向输出 | static-check 四规则 + build |
| REQ-002 | §3 ePWM 关键配置（实证值） | build + DSS H5 回读 + 示波器 |
| REQ-003 | §3 ADC SOCA/A2 契约 | build + SCI adc= 随电压变化 |
| REQ-004 | §3 CMPSS→TZ 链（官方例程范本） | build + DSS 强制越实验 + 无 FAULT 行 |
| REQ-005 | §3 angle_gen 算法（余数累积） | test_angle_gen.c |
| REQ-006 | §3 SVPWM 算法（min-max+守卫） | test_svpwm.c |
| REQ-007 | §3 SCI 打点契约 | run-hil serialChecks 九项 |
| REQ-008 | §6 错误处理表 + 自检顺序 | host self_test + DSS H3 + FAULT 注入 |

## 9. 风险与取舍

| 风险/取舍 | 影响 | 缓解 |
|---|---|---|
| CMPSS→TZ 链在 full-platform 上曾因 DAC 未接入 CMPHPMUX 而板上验证失败（mux=255） | 若 motor-pwm 重蹈，H5 证据缺失 | 正端用复位默认 CMPIN1P（与 A2 同脚，实证存在）；自检改双向 COMPSTS 探测，不依赖运行时 mux 扫描 |
| A2 悬空，ADC 读数不可预测 | 固定阈值探针对某些读数无法自洽 | 探针以**实测 ADC 值为基准**（跳断方向 DAC=实测−512）验证；免跳方向改用满量程 DAC（物理上界，规避 ePWM 载波耦合噪声超 512 码窗口）；仍不自洽则诚实 FAIL，绝不伪造 PASS |
| 死区极性语义曾不确定（POLSEL 语义） | B 路可能非互补 | syscfg + epwm_ex8 头注双重实证 POLSEL=2=B 互补；HIL 寄存器回读复核 |
| mag 默认 800‰ 越过线性区 2/3 | 每次运行都走过调制守卫路径 | 设计意图（守卫非死代码）；主机单测覆盖守卫数学 |
| superloop 轮询延迟影响打点周期精度 | motor: 行周期抖动 | 打点只要求"约 10 Hz"（REQ-007 措辞），CPU Timer 时基足够 |
| 定点 sin/cos 表占 flash | flash 预算 | 四分之一波 901 项仅 1802 B（初稿 14.4 KB 的全波双表已否决）；实测 FLASH 5.06%，余量充足 |

---

### 检查清单

- [x] 架构分层符合铁律（纯逻辑无硬件依赖）
- [x] 接口契约明确（原型 + 返回码语义 + 调用方契约）
- [x] 内存/Flash 预算有估算
- [x] 需求覆盖表覆盖全部 REQ
