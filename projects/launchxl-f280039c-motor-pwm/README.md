# projects/launchxl-f280039c-motor-pwm — 三相电机 PWM 骨架（无电机）

LAUNCHXL-F280039C 上的**开环 SVPWM 演示**：开环电角度 → SVPWM → ePWM1/2/3 三相互补输出
（10 kHz 中心对齐载波 + 1 µs 死区），ADCA 由 ePWM1 SOCA 触发采 A2，CMPSS1→X-BAR→DCBEVT1
构成**硬件一次性 trip** 过流保护链。

**研究边界：开发板未接功率板与真实电机**（无电流环、无速度环）。本工程只证明
"调制时序 + 采样触发 + 保护链"这条确定性时基成立，闭环控制属后续 spec 范围。

规格：`specs/0003-motor-pwm-skeleton/`（REQ-001…REQ-008，状态 `done`）。

## 目录

```
launchxl-f280039c-motor-pwm/
├── Makefile                          ← 经 ccs.ps1 调用（禁止裸调），LDFLAGS 必须带 --args=0x200（ADR 0004）
├── project.config.json               ← 工程配置（契约 harness/schemas/project.config.schema.json）
├── state.json                        ← 运行时状态快照（契约 state.schema.json）
├── 28003x_motor_pwm_flash_lnk.cmd    ← Flash 链接脚本（.text 分 .text.1/.text.2 两片）
├── 28003x_motor_pwm_ram_lnk.cmd      ← RAM 链接脚本
├── device/f28003x_codestartbranch.asm
├── targetConfigs/TMS320F280039C_LaunchPad.ccxml   ← cJTAG 2-pin（必须）
├── tools/                            ← 波表生成等辅助脚本
└── source/
    ├── angle_gen.h/.c    纯逻辑：开环电角度（64 位有理累加，无浮点漂移）
    ├── svpwm.h/.c        纯逻辑：SVPWM 调制（min-max 定心零矢量）+ 过调制守卫
    ├── svpwm_table.h     四分之一波 sin 表：901 项 × 2 B = 1802 B
    ├── fmt.h/.c          纯逻辑：数字格式化（禁用 sprintf）
    ├── self_test.h/.c    纯逻辑：上电自检参考序列（主机侧也跑同一份）
    ├── board_pins.h      引脚唯一真相（PWM/ISENSE/LED/SCI）
    ├── board_clock.h/.c  时钟（INTOSC2 → 120 MHz SYSCLK）
    ├── board_epwm.h/.c   ePWM1/2/3 时基 + 死区 + AQ + SOCA 触发
    ├── board_adc.h/.c    ADCA SOC0（A2）+ 基线阻塞读
    ├── board_cmpss.h/.c  CMPSS1 DAC/比较器 + TZ 链武装与清除
    ├── board_error.c     driverlib ASSERT 钩子 __error__
    ├── sci_log.h/.c      SCIA 轮询日志（115200-8N1）
    ├── app_version.h     版本与构建时间
    └── main.c            ramfunc 重定位 → 禁狗 → PIE → 时钟 → 外设 → 自检 → superloop
```

## 引脚（唯一真相：`source/board_pins.h`）

| 信号 | GPIO | 说明 |
|---|---|---|
| PWM A 上/下桥 | 0 / 1 | ePWM1A / ePWM1B（互补 + 死区） |
| PWM B 上/下桥 | 2 / 3 | ePWM2A / ePWM2B |
| PWM C 上/下桥 | 4 / 5 | ePWM3A / ePWM3B |
| ISENSE | A2（pin 17） | ADCA 通道 2 **与** CMPSS1 正端 CMPIN1P 共脚，无 GPIO mux |
| LED4 / LED5 | 20 / 22 | 红 / 绿，**低电平点亮** |
| SCI TX / RX | 29 / 28 | XDS110 虚拟 COM，115200-8N1 |

GPIO35–38 是 cJTAG 调试脚，**永不作为 GPIO 复用**（复用会永久废掉 JTAG）。

## 时序参数（实测值，`specs/0003-motor-pwm-skeleton/design.md` §3）

| 参数 | 寄存器 | 值 | 物理含义 |
|---|---|---|---|
| 载波 | TBCTL.CTRMODE / TBPRD | up-down / 6000 | 120 MHz ÷ (2×6000) = **10 kHz** |
| 死区 | DBRED / DBFED | 120 | **1 µs** @120 MHz，防上下桥直通 |
| 互补 | DBCTL.POLSEL | 2 | OUTB = ¬OUTA |
| 动作 | AQ | ZRO→SET / UP_CMPA→CLEAR / DOWN_CMPA→SET | 占空比 = CMPA/PRD |
| 采样触发 | EPWM1 SOCA + ETPS.SOCAPRD | TBCTR=PRD / **1** | 载波谷底触发，避开开关噪声 |
| 保护 | TZSEL.DCBEVT1（经 X-BAR TRIP4） | 一次性锁存 | 硬件关 6 路，零软件介入 |

### 三个必须记住的字地址事实（ADR 0002）

1. **C28x 是 16 位字寻址**：`sizeof(uint32_t)=2 字`，DSS `readWord()` 只返回半个 32 位寄存器。
2. **CMPA 是 32 位寄存器对**：`CMPA:CMPAHR` @ 字 `0x6A`/`0x6B`，**有效占空比在高字 `0x6B`**
   （driverlib 写 `HWREGH(base+CMPA+1)`）。读 `0x6A` 恒为 0 → 会制造"CMPA 没变"的假 FAIL。TBPRD 同理读 `0x63`。
3. **LED 位在 GPADAT 高字**：GPIO20 = 字 `0x7F01` bit4，GPIO22 = 同字 bit6；只读低字会得到"全灭"假象。

## SCI 契约（115200-8N1，`run-hil.ps1` 的 serialChecks 断言这些行）

```
=== LAUNCHXL-F280039C motor-pwm ===
version: ...
pwm: prd=6000 db=120 pol=2 OK          ← 死区/极性/周期回读自检
cmpss: adc=<baseline> hi=1 lo=0 OK     ← 双向比较器探测（hi/lo 是比较器实际状态 COMPSTS）
tz: chain armed                        ← 自检通过后才武装 TZ（防自检期误触发）
self-test: svpwm/angle/fmt OK
motor: ang=<0.01Hz> mag=<‰> a=<cmpa> b=<cmpa> c=<cmpa> adc=<raw>
FAULT: tz-latched                      ← 过流一次性 trip
tz: cleared                            ← dwell 5 s 且比较器回落后的自主恢复
```

`cmpss:` 行的 `hi=`/`lo=` 打印的是**比较器实际状态**，不是"自检是否匹配"的布尔；
判定准入条件是 `board_cmpss_wait_status()` 的两个匹配布尔 `hi_ok && lo_ok`（API 语义见 design.md）。

## 构建 / 验证

```powershell
# 构建（默认 CPU1_FLASH）
.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-motor-pwm

# 体积（C28x 字口径）
.\harness\tools\ccs.ps1 size  -Project projects\launchxl-f280039c-motor-pwm

# 主机单测（纯逻辑 41 用例）
.\harness\tools\run-host-tests.ps1

# 全门禁 + 上板 HIL（需 XDS110 在位）
.\harness\tools\verify.ps1 -Project launchxl-f280039c-motor-pwm -Hil
```

### 体积基线（`ccs.ps1 size`，CPU1_FLASH）

| 段 | 字 | 字节 |
|---|---|---|
| .text（.text.1 4096 + .text.2 3361） | 7457 | 14914 |
| .const（含 sin 表） | 2716 | 5432 |
| .cinit / .TI.ramfunc / codestart | 28 / 4 / 2 | — |
| **FLASH 合计** | **10207** | 5.19 % of 196608 字 |
| **RAM 合计**（.args 512 + .stack 512 + .data 14 + .bss 2） | **1040** | 2.94 % of 35328 字 |

> `.text.2 = 0xD21` 是 **16 进制 = 3361 字**，不是十进制；按十进制读会把体积算错（ADR 0002）。

### HIL 证据

- 2026-10-25：`HIL RESULT: PASS`（H1–H6），记录
  `harness/tests/on-target/records/hil-2026-10-25-launchxl-f280039c-motor-pwm.md`
- 2026-10-31：`verify.ps1 -Hil` **VERIFY OK - 9 legs PASS**，记录
  `harness/tests/on-target/records/hil-2026-10-31-launchxl-f280039c-motor-pwm.md`

判据实现：`harness/tools/dss/hil_test_mp.js`。
**恢复判定读 CMPA 变化，不读瞬时 GPADAT_R** —— 中心对齐 + 本 AQ 序列下占空比=PRD 的相
整周期为低，瞬时读全零是正常波形而非"未恢复"。

## 待人工补测（示波器/逻辑分析仪，无外部仪器前不声称已验证）

- [ ] 互补边沿间 **1 µs 死区**实测
- [ ] 载波 **10 kHz** 实测
- [ ] 三相相位差 **120°** 实测
- [ ] **断电冷启动**（Stand-Alone Boot）人工执行一次

清单维护在 `harness/tests/on-target/README.md`。

## 安全

接功率板前：示波器探地夹只能接隔离地或用差分探头；上电前先确认串口出现
`tz: chain armed`（TZ 链已武装）再放 PWM。CMPSS DAC 上电默认满量程 4095
（浮置噪声不误触发）就是为此设计的。
