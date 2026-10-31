# design.md — 全外设并存平台工程（full-platform）

> 状态：`design-approved`（用户 2026-10-24 指示"按你的设计开展"，对应本文蓝图）｜ 上游：`requirements.md`（REQ-001…REQ-022）
> 硬数据来源：C2000Ware_26_01_00_00 的 driverlib/f28003x/driverlib/（pin_map.h、inc/hw_memmap.h、各外设头文件，SDK 安装目录 D:\ti\ccs2101\C2000Ware_26_01_00_00 下）、device_support/f28003x/common/cmd/28003x_cla_flash_lnk.cmd、examples/cla。

## 1. 概述

新建独立工程 `projects/launchxl-f280039c-full-platform`，在一张 F280039C 上让全部处理器子系统并存工作。
核心思路三条：

1. **内部回环优先**——F28003x 的回环机制经 SDK 实证全部存在（见 §8 证据表）：
   SPI/SCI/I2C/LIN/MCAN/FSI 都有内部回环模式，DAC→ADC 有内部模拟回环（`ASysCtl_enableADCDACLoopback`），
   ePWM→eCAP/eQEP 走 **pad 回环**（ePWM 输出到 GPIO，Input X-BAR 选同一 GPIO 读回，TI 例程标准用法）。
   同板跳线仅用于**物理层增强证据**（I2C A↔B 主从、FSI TX→RX）。
2. **统一调度 + 健康监视**——全部子系统注册为任务描述符，由纯逻辑调度器按 1ms/10ms/100ms/1s 分频驱动；
   每个子系统维护心跳/错误计数，聚合健康状态字每 5s 从 SCIA 输出一行，HIL 与 soak 都以它为判据。
3. **harness 多工程化**——主机单测 manifest（modules.json）从单工程扩展为多工程结构，
   参考工程 led-chaser 的既有用例零改动继续跑（REQ-001"不修改参考工程任何文件"）。

## 2. 硅片事实基线（hw_memmap.h / pin_map.h 实测提取，2026-10-24）

| 外设 | 实例数 | 说明 |
|---|---|---|
| ePWM | **8 模块**（EPWM1–8，A/B × 8 = **16 通道**） | 需求措辞修正：REQ-008"16 路" = 16 通道 |
| eCAP | 3（ECAP1–3）；**HRCAP 仅 ECAP3** | HRPWM 高分辨率测量走 ECAP3 |
| eQEP | 2 | |
| SPI / I2C / SCI / LIN | 2 / 2 / **2（A、B，无 SCIC）** / 2 | 需求修正：REQ-013 去 SCIC，改 SCIB + LINA + LINB |
| MCAN | 1（MCANA） | |
| FSI | 1 TX + 1 RX | `FSI_RX_MAIN_CTRL_INT_LOOPBACK` 内部回环存在 |
| CMPSS | 4 | 正端经 CMPHPMUX 选 ADCIN 引脚，负端=内部参考 DAC |
| DAC | **2（DACA/DACB，独立外设）** | DAC→ADCA/ADCC 内部模拟回环 API 存在 |
| DMA | 6 通道 | |
| CLB | **4 tiles**（CLB1–4） | |
| CLA | 1（CLA1） | 链接模板 `28003x_cla_flash_lnk.cmd` |
| DCC | 2（DCC0/1） | |
| ADC | 3（A/B/C） | |
| CPUTimer | 3 | |
| EPG | 1（EPG1+EPG1MUX） | |
| BGCRC / AES / ERAD | CPU BGCRC ×1 / AES ×1 / ERAD 存在 | |
| Input X-BAR | INPUT1–16 | eCAP/eQEP/TZ 输入皆经此 |
| Output X-BAR | OUT1–8 | |

需求侧修正（硅片事实，随本设计一并落回 requirements.md/spec.json）：
REQ-008 措辞改为"8 模块 16 通道"；REQ-013 的 SCIC 改为 LINB（保留 SCIB）；
简介的"ePWM×16/SCI×3"同步更正。未决项 1/3/4 按 §8 证据更新状态。

## 3. 架构与分层

```
main.c（full-platform）
  0)  board_ramfunc_relocate() + ready 校验        ← ADR 0003 启动硬约束
  0b) SysCtl_disableWatchdog()
  0c) Interrupt_initModule() + initVectorTable()
  1)  board_clock_init()（INTOSC2→120MHz 校验）
  2)  各子系统 xxx_init()：先 SysCtl_enablePeripheral(对应 PCLKCRn)（ADR 0005）
      → GPIO mux（全部经 board_pins.h 符号）→ 外设配置 → 回环使能
  3)  self_test_run()（扩展自检：pattern/fmt/scheduler/health/参考值）
  4)  使能窗口看门狗（子系统初始化完成后，REQ-018）
  5)  super-loop：
        for each 任务 in 调度表（按到期序）：task.tick()
        喂狗（窗口内）→ 健康 5s 行 → LED 形态输出
```

**分层铁律不变**：source/<module>.c 纯逻辑（主机可测）/ source/<module>_hw.c driverlib 层 /
source/board_*.c 系统级 / source/cla/ CLA 侧源（.cla，M4 创建）。
纯逻辑模块清单（主机单测对象）：`fmt`、`led_pattern`（自 led-chaser 复制，工程自包含）、
`scheduler`、`health`、`frame_gen`（M3：SPI/MCAN/FSI 测试帧与参考校验值）、
`self_test`（扩展）。TMU/VCRC 指令是编译器 intrinsic，**只在目标侧存在**，主机测不了——
其参考值计算用纯 C 实现放 `math_ref.c`（主机可测），目标侧 TMU 结果与 `math_ref` 比对。

## 4. 调度器与并发（REQ-001/003）

```c
/* scheduler.h —— 纯逻辑 */
#define SCHED_MAX_TASKS 32U
typedef void (*sched_task_fn)(void *ctx, uint32_t now_ms);
typedef struct {
    const char *name;         /* ≤7 字符，健康行用 */
    sched_task_fn tick;       /* 到期被调 */
    uint16_t period_ms;       /* 0 = 每圈 */
    uint16_t phase_ms;        /* 错峰 */
    /* 运行态（由调度器维护）： */
    uint32_t last_run_ms; uint32_t run_count; int32_t max_late_ms;
} sched_task_t;
void   sched_init(uint32_t now_ms);
int16_t sched_register(sched_task_t *t);          /* 返回槽位或 -1 */
void   sched_run_due(uint32_t now_ms);            /* 主循环每圈调 */
bool   sched_task_late(const sched_task_t *t, uint32_t now_ms, uint16_t tol_ms);
```

- 时基：CPUTimer0 产生 1ms 硬件节拍（时基=SYSCLK，ADR 0005，`TPR=0`、`RUNFREE`），
  主循环**轮询** TIF（沿用参考工程模式，ISR 最小化；三 Timer 的 TIF 都在主循环读）。
  CPUTimer1 = 10ms 粗时基、CPUTimer2 = 1s 统计时基（REQ-003 三 Timer 全用）。
- `sched_run_due` 只做"到期判断 + 调 tick"，**禁止任务内忙等**（REQ-001）；
  通信等待（如 SPI 完成）用状态机分片，超时计数进健康。
- 任务周期分层：1ms（ADC 扫描推进、DMA 状态机）/ 10ms（SPI/I2C/SCIB/LIN 轮询步进、
  eCAP/eQEP 读数）/ 100ms（MCAN 窗口、CLA 结果比对、DAC 阶梯步进）/ 1s（BGCRC 增量校验、
  TMU 批任务）/ 5s（健康行、心跳）。超窗带宽：任务迟到 >2×period 记一次健康错误（初值，可调）。

## 5. 健康监视与 fail-safe（REQ-020）

```c
/* health.h —— 纯逻辑 */
typedef struct {
    uint16_t id;                 /* 位号（健康映射字用） */
    const char *name;            /* ≤7 字符 */
    uint32_t beats;              /* 心跳：任务成功 tick 递增 */
    uint32_t errors;             /* 校验失败/超时递增 */
    uint16_t err_threshold;      /* 连续错误 fail-safe 阈值（must 子系统默认 3） */
} health_mon_t;
void     health_reset(void);
void     health_beat(uint16_t id);
void     health_fail(uint16_t id);
uint32_t health_map(void);       /* 位图：bit=1 表示该子系统 errors>0 */
bool     health_should_failsafe(void);
uint32_t health_worst(void);     /* 错误最多的子系统 id（打印用） */
```

- 聚合行（每 5s，SCIA）：`health: up=<秒> map=<hex> err=<总数> hb=<最小心跳>`；
  HIL 直接读符号 `g_health_map`、`g_health_err_total`、各子系统 `g_<name>_beats`。
- fail-safe：must 子系统 `errors ≥ 阈值` → LED 双灯 100ms 同闪（复用 led_pattern）+
  `health: FAILSAFE <id>` 持续打印；should/could 子系统失败只计健康不进 fail-safe（降级，REQ-001）。
- 主机单测：`test_health.c`（阈值/位图/恢复）、`test_scheduler.c`（到期/错峰/迟到统计）。

## 6. 引脚矩阵（REQ-022；候选来自 pin_map.h，2026-10-24 提取）

已占用不可动：GPIO20/22（LED4/5，低有效）、GPIO28/29（SCIA console）、GPIO35–38（**JTAG 红线**）。
原则：pad 回环所需的 ePWM 输出与 Input X-BAR 读回**同一 GPIO**；内部回环的外设引脚仅作登记
（回环不依赖引脚），跳线只服务物理层增强证据。

| 功能 | GPIO | mux 宏（board_pins.h 符号） | 用途 |
|---|---|---|---|
| EPWM1A | 0 | GPIO_0_EPWM1_A | eCAP1 经 INPUT7 回灌测频 |
| EPWM2A | 2 | GPIO_2_EPWM2_A | eCAP2 经 INPUT8 回灌 |
| EPWM3A | 4 | GPIO_4_EPWM3_A | eCAP3(HRCAP) 经 INPUT9 回灌，HRPWM 通道 |
| EPWM4A/B | 6/7 | GPIO_6_EPWM4_A / GPIO_7_EPWM4_B | 正交对 1 → eQEP1（IN 10/11） |
| EPWM8A/B | 14/15 | GPIO_14_EPWM8_A / GPIO_15_EPWM8_B | 正交对 2 → eQEP2（IN 12/13） |
| SPIA SIMO/SOMI/CLK | 16/17/18 | GPIO_16_SPIA_SIMO 等 | 数字回环（引脚登记） |
| SPIB SIMO/SOMI/CLK | 24/25/26 | GPIO_24_SPIB_SIMO 等 | 数字回环 + DMA |
| I2CA SDA/SCL | 10/1 | GPIO_10_I2CA_SDA / GPIO_1_I2CA_SCL | 主机；跳线→I2CB |
| I2CB SDA/SCL | 34/33 | GPIO_34_I2CB_SDA / GPIO_33_I2CB_SCL | 从机；跳线 10↔34、1↔33 |
| SCIB TX/RX | 12/13 | GPIO_12_SCIB_TX / GPIO_13_SCIB_RX | 内部回环（引脚登记） |
| LINA TX/RX | 32/23 | GPIO_32_LINA_TX / GPIO_23_LINA_RX | 数字回环 |
| LINB TX/RX | 44/41 | GPIO_44_LINB_TX / GPIO_41_LINB_RX | 数字回环 |
| MCANA TX/RX | 48/49 | GPIO_48_MCAN_TX / GPIO_49_MCAN_RX | 自测回环（引脚登记） |
| FSI TX_D0/CLK | 45/51 | GPIO_45_FSITXA_D0 / GPIO_51_FSITXA_CLK | 跳线 45↔3、51↔39（可选） |
| FSI RX_D0/CLK | 3/39 | GPIO_3_FSIRXA_D0 / GPIO_39_FSIRXA_CLK | 内部回环为主判据 |
| ADC 扫描通道 | 实现期定 | — | ≥6 个 ADCIN，从数字未占用的 ADCIN 引脚选（TRM 引脚表核对），DACA/DACB 回环通道按实证通道号登记 |
| CMPSS1 正端 | 实现期定 | — | 选一个与 ePWM 输出复用的 ADCIN 引脚（pad 回环数字电平进比较器），负端=内部参考 DAC |

冲突校验：上表每个 GPIO 恰一个职能（0,1,2,3,4,6,7,10,12,13,14,15,16,17,18,23,24,25,26,32,33,34,39,41,44,45,48,49,51 互不重复；20/22/28/29/35–38 保持原职能）。EPWM5/6/7 模块**运行但不引脚**（寄存器判读，REQ-008 允许）；EPWM7A=GPIO12 与 SCIB_TX 冲突→SCIB 用内部回环不依赖引脚，mux 登记为 SCIB，EPWM7 不引脚。

## 7. 子系统设计（按 REQ 序）

| REQ | 子系统 | 设计要点 | 回环/判读路径 |
|---|---|---|---|
| 002 | DCC×2 | DCC0 校 SYSCLK vs INTOSC1（计数窗），10ms 任务读 DCC 状态；连续 3 错→健康+fail-safe | 寄存器状态 + 健康行 |
| 004 | CLA | 移植 SDK cla 例程模式：`source/cla/app_cla.cla`（Task1 = 8 点 FIR 或 FNV-1a，M4 定）+ `_shared.h`；Cla1Prog LOAD=FLASH_SEC RUN=RAMLS1（拷贝进 startup，与 ramfunc 同机制）；消息 RAM CLA1_MSGRAMLOW@0x1480/HIGH@0x1500 交换；CPU 参考值来自 `math_ref.c` | 结果比对计数进健康 |
| 005 | VCRC/TMU/AES/BGCRC | VCRC：intrinsics 计算 `frame_gen` 缓冲 CRC，对照主机参考常量；TMU：`__sin/__atan` 批算 vs `math_ref`（纯 C 参考，相对误差带宽 1e-4）；AES：单块 128bit ECB 加解密往返（流程按 aes.h）；BGCRC：CPU BGCRC 配 .text 区起始段增量校验（每 1s 一窗） | 各自校验计数进健康 |
| 006 | ADC×3 | 每模块 2 SOC（一固定通道 + DAC 回环通道[ADCA/ADCC]），软件触发轮询；统计 min/max/avg 纯逻辑放 `adc_stat.c`（主机可测） | 统计值 + 完成计数 |
| 007 | DAC×2 + CMPSS | DACA 阶梯（8 级）→ ADCA 回环读回比对；DACB 固定电平→ADCC；CMPSS1：正端=回灌 ePWM 方波的 ADCIN 引脚，负端=内部 DAC 阈值，CTRIPH 经 X-BAR→EPWM1 TZ 动作 + 计数 | ADC 读数一致性 + TZ 触发计数 + CMPSS 翻转计数 |
| 008 | ePWM 8 模块 | 4 频率组：10kHz(EPWM1)/20kHz(EPWM2)/50kHz+HRPWM(EPWM3)/1kHz 正交(EPWM4)/5kHz 正交(EPWM8)；EPWM5–7 10kHz 变占空比不引脚；TZ：CMPSS1→EPWM1（TRIPINPUT 经 X-BAR） | 寄存器回读 + eCAP 实测 |
| 009 | eCAP×3 | INPUT7/8/9 ← GPIO0/2/4；连续模式测周期/占空比，均值滤波；ECAP3 开 HRCAP | 实测频率 vs 配置（±2% 带宽，样本 ≥64） |
| 010 | eQEP×2 | INPUT10/11←GPIO6/7、INPUT12/13←GPIO14/15；QPOSCNT 位置、QEPSTS 方向、MCR 速度（unit timer=SYSCLK/128） | 位置单调 + 方向翻转 + 速度带宽 |
| 011 | SPI×2 | SPI_enableLoopback，8 字节 PRBS 块（`frame_gen` 生成+校验）；SPIB 收发走 DMA CH2/3 | 逐字节比对计数 |
| 012 | I2C×2 | 主判据：I2CA controller 内部回环；物理层增强：A↔B 跳线（内部上拉，100kHz 降 50kHz 若不稳）读写 8 字节 | 比对计数；跳线失败→健康 warn 不 fail-safe |
| 013 | SCIB/LINA/LINB | SCI_enableLoopback、LIN_LOOPBACK_DIGITAL；固定报文（banner 风格）收发 | 比对计数 |
| 014 | MCANA | MCAN_setOpMode LOOPBACK；std+ext 帧各 1/100ms 窗，RX FIFO 元素比对 | 收发计数 + 协议错误计数=0 |
| 015 | FSI | 主判据：RX 内部 loopback，128bit 帧 ×N；可选跳线 45↔3/51↔39 实证后登记 | 帧比对计数 |
| 016 | DMA×6 | CH1 ADC 结果、CH2/3 SPIB TX/RX、CH4 MCAN RX、CH5 内存块搬运（FNV 校验）、CH6 LINA RX（按实现微调）；全部软件触发+完成计数 | 每通道完成计数单调 + 数据校验 |
| 017 | CLB×4 | 选型（未决项 5）：优先 driverlib `clb.h` 寄存器级 API 配 CLB1 计数器（无 SysConfig 依赖，可 diff 评审）；CLB2–4 保持复位态不初始化（存在性由基址表与 CLB1 同构证明） | CLB1 计数 vs 理论 |
| 018 | 看门狗 | 全部子系统 init 完成后使能窗口看门狗（WDPS/窗口按 SYSCLK 预算选，最坏任务链 <10ms）；主循环每圈 `SysCtl_serviceWatchdog`；RESC 在 banner 解码打印 | soak 零 WDRSn；故障注入单独验 |
| 019 | EPG | EPG1 经 EPGMUX 做内部图案注入尝试（epg.h API 齐备）；无零外设注入路径则寄存器级登记（open_questions） | 判读或登记 |
| 021 | HIL/soak | hil_test.js 增 H7（健康字零错+心跳单调）、H8（eCAP 频率符号读数）、H9（eQEP 位置/速度）、H10（DMA 计数）、H11（通信簇计数）；`run-hil.ps1 -SoakMinutes 30` 模式（轮询健康行 + 结束符号快照，记录落 records/）；日常档 2min | 符号读数 + SCI 捕获 |

## 8. 回环机制证据表（SDK 头文件/例程实测，2026-10-24）

| 机制 | API/位 | 来源 |
|---|---|---|
| SPI 内部回环 | `SPI_enableLoopback(base)` | driverlib/spi.h:1077 |
| SCI 内部回环 | `SCI_enableLoopback(base)` | driverlib/sci.h:1326 |
| LIN 回环 | `LIN_LOOPBACK_DIGITAL/_ANALOG` | driverlib/lin.h:231 |
| I2C 回环（controller 模式） | `I2C_enableLoopback` | driverlib/i2c.h:1066 |
| MCAN 回环 | `MCAN_setOpMode(base, MCAN_OperationMode)` | driverlib/mcan.h:1404 |
| FSI RX 内部回环 | `FSI_RX_MAIN_CTRL_INT_LOOPBACK`（enable/disable inline） | driverlib/fsi.h:1740 |
| DAC→ADC 模拟回环 | `ASysCtl_enableADCDACLoopback(ENLB2ADCA\|ENLB2ADCC)`（KEY 0xA5A5） | driverlib/asysctl.h:731 |
| CMPSS 负端=内部 DAC | `CMPSS_configDAC` + `CMPSS_configHighComparator` | driverlib/cmpss.h:545 |
| CMPSS→ePWM TZ | CMPSS CTRIPH 经 ePWM X-BAR TRIP 输入（DCAEVT/TZSEL） | epwm.h TZ API |
| pad 回环 | ePWM→GPIO 输出 + Input X-BAR（INPUT1–16）选同 GPIO→eCAP/eQEP | pin_map.h + xbar.h |
| BGCRC / AES / EPG / VCRC-intrinsic / TMU-intrinsic | bgcrc.h / aes.h / epg.h / CGT intrinsic / CGT intrinsic | driverlib 各头 |

未决项状态更新（随本设计）：#1 FSI→**已解**（内部回环存在；跳线降为可选物理层证据）；
#3 SCIB/LIN→**已解**（内部回环 API 存在）；#4 DAC→ADC→**已解**（内部模拟回环存在，通道号 M2 实证）；
#2 I2C A↔B 跳线上拉→保持 pending（上板实证）；#5 CLB 来源→保持 pending（M4 选型，倾向 clb.h 寄存器级）。

## 9. 内存与体积预算

- 链接脚本：以 led-chaser 的 flash/ram cmd 为基（已含 .TI.ramfunc 与 `--args` 经验），
  增加 SDK `28003x_cla_flash_lnk.cmd` 的 CLA 段（Cla1Prog LOAD=FLASH_BANK0_SECn RUN=RAMLS1、
  CLA1_MSGRAMLOW@0x1480/HIGH@0x1500）；ramfunc 保持 RAMLS0，.ebss 让到 LS5/LS6（照 SDK 模板）。
- RAM 预算：health/scheduler 表 <1KB；ADC/DMA 缓冲 4×256 字；帧缓冲 <2KB；合计 <10KB / 35.3KB。
- Flash 预算：全 driverlib 链接 + CLA + 16 通道 ePWM 配置，.text 估 40–60K 字 / 192K（<30%）；
  门禁仍 90%，预算内。`--args=0x200` 保留（ADR 0004）。
- 无动态内存；无 VLA；缓冲全部静态定长。

## 10. 主机侧与 harness 变更

1. **modules.json 多工程化**（harness 变更，非参考工程文件）：
   v2 结构按工程分组（`projects.<name>.{source_dir,logic[],suites[]}`），runner/CMake 合并编译两工程
   纯逻辑模块（fw_logic_led_chaser + fw_logic_full_platform 静态库 → 单 test_runner）；
   static-check 的 LAY/TEST 规则按工程分组校验；led-chaser 的用例与声明**零语义变化**（回归要求：
   改后 led-chaser 的 verify 全绿）。
2. 新增纯逻辑模块（full-platform）：`scheduler`、`health`、`math_ref`（M2）、`frame_gen`（M3）、
   `adc_stat`（M2）、`self_test`（扩展版）；对应 suites：`test_scheduler.c`、`test_health.c`，
   后续里程碑补 `test_math_ref.c`、`test_frame_gen.c`、`test_adc_stat.c`。
3. run-hil.ps1：`-SoakMinutes` 参数 + H7–H11 判读（M5 实现，负向自测按 ADR 0009）。

## 11. 里程碑（tasks.md 的实现顺序，每步 verify 全绿再进）

- **M1 工程骨架**：Makefile/链接脚本/启动/时钟/console/LED + scheduler + health + 扩展 self_test +
  主机单测（scheduler/health）+ harness 多工程化 + state.json/README。
- **M2 采集与控制**：DCC、ADC×3+统计、DAC×2+CMPSS+TZ、ePWM 8 模块、eCAP×3(HRCAP)、eQEP×2、
  Input X-BAR、math_ref/adc_stat 主机模块。
- **M3 通信与 DMA**：SPI×2、I2C×2（内部回环 + 跳线实证）、SCIB/LINA/LINB、MCAN、FSI、frame_gen、
  DMA×6。
- **M4 加速器与安全**：CLA 任务+链接、VCRC/TMU/AES、BGCRC、CLB、EPG、窗口看门狗启用。
- **M5 收口**：HIL H7–H11、soak、故障注入、文档/ADR/state 回写、`verify.ps1 -Hil` 全绿。

## 12. 需求覆盖表

| REQ | 设计落点 | 验证 |
|---|---|---|
| 001 | §3 分层 + §4 调度器 + §10 harness 多工程 | build/static-check/host-tests + led-chaser 回归 verify |
| 002 | §7 DCC 行 | banner sysclk 行 + 健康 dcc=0 |
| 003 | §4 三 Timer 时基 + 迟到统计 | test_scheduler + H7 心跳单调 |
| 004 | §7 CLA 行 + §9 链接 | CLA 段链接 + 健康 cla 计数 |
| 005 | §7 加速器行 + math_ref | test_math_ref + 健康 bgcrc/aes/vcrc/tmu |
| 006 | §7 ADC 行 + adc_stat | 统计行 + 健康 adc=0 |
| 007 | §7 DAC/CMPSS 行 + §8 证据 | ADC 读回比对 + TZ 计数 |
| 008 | §6 引脚 + §7 ePWM 行 | 寄存器回读 + H8 |
| 009 | §7 eCAP 行 | H8 实测频率 ±2% |
| 010 | §7 eQEP 行 | H9 位置/速度 |
| 011 | §7 SPI 行 | 健康 spi 计数 |
| 012 | §7 I2C 行 | 健康 i2c 计数（跳线实证记录） |
| 013 | §7 SCIB/LIN 行 | 健康 sci/lin 计数 |
| 014 | §7 MCAN 行 | 健康 mcan 计数 |
| 015 | §7 FSI 行 | 健康 fsi 计数 |
| 016 | §7 DMA 行 | H10 计数单调 |
| 017 | §7 CLB 行 | 健康 clb 计数 |
| 018 | §7 看门狗行 | soak 零复位 + 故障注入记录 |
| 019 | §7 EPG 行 | 判读或 open_questions 登记 |
| 020 | §5 health | H7 + 5s 健康行 + 故障注入 |
| 021 | §7 HIL/soak 行 | run-hil 全 PASS + soak 记录 |
| 022 | §6 引脚矩阵 | static-check 0 violations |

## 13. 风险与取舍

| 风险 | 缓解 |
|---|---|
| pad 回环的物理层依赖引脚驱动/输入缓冲（未上拉引脚噪声，ADR 0002 教训） | 回灌判读只认 eCAP/eQEP 计数与 GPADAT_R 读回，不读锁存低字；采样均值滤波 |
| I2C 内部上拉太弱撑不住 A↔B 跳线 | 主判据已定为内部回环；跳线失败只降级健康 warn |
| DAC 回环通道号未知（driverlib 未注明落点 ADCIN） | M2 实证：使能回环后扫全部 ADCIN 通道找响应者，结果回写本表与 targets 卡 |
| 全外设同开的中断/总线负载 | 零 ISR 设计（主循环轮询），DMA 代替 CPU 搬运；soak 30min 兜底 |
| CLA 工具链复杂度（.cla 编译、段拷贝） | 链接模板直接取 SDK 官方 cmd；Cla1Prog 拷贝复用 ramfunc 机制；M4 单独里程碑隔离风险 |
| eQEP 正交两 PWM 相位 90° 配置错误（方向抖动） | eQEP QEPSTS 方向位 + 位置单调性双判据；H9 带宽放宽（理论 ±10% 起步） |
| harness 多工程化破坏既有门禁 | 先改 modules.json → 立即跑 led-chaser verify 全绿作为回归门（ADR 0009 正向+负向自测） |

---

### 检查清单

- [x] 架构分层符合铁律（§3）
- [x] 接口契约明确（§4/§5 代码块）
- [x] 引脚矩阵零冲突（§6）
- [x] 内存/Flash 预算有估算（§9）
- [x] 需求覆盖表覆盖全部 22 条 REQ（§12）
- [x] 回环机制全部有 SDK 证据（§8）
