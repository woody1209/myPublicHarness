# projects/launchxl-f280039c-led-chaser — LED 流水灯

LAUNCHXL-F280039C 上的两颗用户 LED 流水灯：**LED4（红，GPIO20）** 与
**LED5（绿，GPIO22）**，**低电平点亮**，默认步进 **200 ms**。

该工程是 `f280039c-harness` 的参考工程：所有板级软件变更都从这里出发，
并且必须走 `harness/tools/ccs.ps1` + `run-host-tests.ps1` + `spec.ps1 check` 三道收口。

规格：`specs/0001-led-chaser/`（REQ-001…REQ-005，状态 `done`）。

## 目录

```
launchxl-f280039c-led-chaser/
├── Makefile                          ← 经 ccs.ps1 调用（禁止裸调）
├── project.config.json               ← 工程配置（契约 harness/schemas/project.config.schema.json）
├── state.json                        ← 运行时状态快照（契约 state.schema.json）
├── 28003x_led_chaser_flash_lnk.cmd   ← Flash 链接脚本（源自 C2000Ware 通用模板）
├── 28003x_led_chaser_ram_lnk.cmd     ← RAM 链接脚本
├── device/f28003x_codestartbranch.asm
├── targetConfigs/TMS320F280039C_LaunchPad.ccxml   ← cJTAG 2-pin（必须）
└── source/
    ├── led_pattern.h/.c   纯逻辑：图样引擎 + 极性映射 + 参考常量（主机可测）
    ├── fmt.h/.c           纯逻辑：带边界的数字格式化（禁用 sprintf）
    ├── self_test.h/.c     纯逻辑：上电自检（主机侧也跑同一份）
    ├── board_pins.h       引脚唯一真相（LED/SCI）
    ├── board_clock.h/.c   时钟（INTOSC2 → 120MHz）+ CPU Timer 节拍
    ├── board_error.c      driverlib ASSERT 钩子 __error__
    ├── led_hw.h/.c        GPIO 适配层
    ├── sci_log.h/.c       SCIA 轮询日志（115200-8N1）
    ├── app_version.h      版本与构建时间
    └── main.c             启动 → 自检 → 节拍循环
```

## 构建 / 验证

```powershell
# 构建（默认 CPU1_FLASH；产物 CPU1_FLASH/*.out）
.\harness\tools\ccs.ps1 build   -Project projects\launchxl-f280039c-led-chaser

# RAM 构建（调试迭代，掉电丢失）
.\harness\tools\ccs.ps1 build   -Project projects\launchxl-f280039c-led-chaser -Config CPU1_RAM

# 体积
.\harness\tools\ccs.ps1 size    -Project projects\launchxl-f280039c-led-chaser

# 主机侧单测（led_pattern / fmt / self_test）
.\harness\tools\run-host-tests.ps1

# 烧录（需先 ccs.ps1 probe 看到 XDS110）
.\harness\tools\ccs.ps1 program -Project projects\launchxl-f280039c-led-chaser -Yes
```

## 行为约定

| 项 | 值 |
|---|---|
| 时钟 | INTOSC2 10MHz → PLL → SYSCLK 120MHz，LSPCLK 30MHz（未达 120MHz 直接 fail-safe） |
| 步进 | 200 ms（改 `source/main.c` 的 `LEDCHASER_STEP_MS` 线性变化；CPU Timer0 计时） |
| 模式 | `LEDCHASER_MODE_DEFAULT`（`main.c`）：`CHASE` / `BOUNCE` / `BLINK` 三选一 |
| 点灯极性 | active-low：掩码位=1 → GPIO 写 0（亮） |
| 心跳 | 每 5 s 一行 `alive, steps=<n>, mode=<m>`（SCI，115200-8N1） |
| 自检失败 | fail-safe：双灯 100 ms 同拍闪烁 + 打印 `self-test FAIL` + 全局 `g_self_test_code` |
| driverlib 断言 | `--define=DEBUG` 生效，失败时调 `__error__` → ESTOP0 停住（`source/board_error.c`） |
| 启动顺序 | 硬约束（ADR 0003）：`main()` 首动作 `.TI.ramfunc` 重定位 + ready 校验 → 禁看门狗 → PIE 初始化 → 时钟 → 外设；违反 = 启动即死（ITRAP→看门狗复位，无日志无灯） |
| loadti 兼容 | Makefile LDFLAGS 含 `--args=0x200`（定义 `__c_args__`，否则 `ccs.ps1 program` 报 4011，ADR 0004）；拷 Makefile 到新工程时勿删 |

## 参考常量（主机与目标共用，禁止两份真相）

- `LED_PATTERN_REF_FNV1A = 0x436FEC05`：CHASE/BOUNCE/BLINK 三模式前 8 拍掩码的 FNV-1a。
- 主机单测另有**独立的逐拍掩码硬编码表**（`test_led_pattern.c`），
  与 FNV-1a 互为交叉验证——改坏实现或改坏参考值都会被测出来。

## 最近一次基线（2026-10-16 构建 / 2026-10-17 更新）

| 项 | 值 |
|---|---|
| 构建配置 | CPU1_FLASH（CPU1_RAM 亦通过） |
| `.text` | 3881 words = 7762 B（C28x 字寻址；旧版本表把 map 的字列写成 "B"，实为 words） |
| `.const` | 1162 words = 2324 B（FLASH） |
| `.data` | 14 words = 28 B（RAM） |
| `.args` | 512 words = 1024 B（`--args=0x200`，loadti 兼容，RAM） |
| Flash 占用 | 5067 / 196608 words = **2.58 %**（`ccs.ps1 size`，含 codestart/.cinit/.const/.text/.TI.ramfunc） |
| RAM 占用 | 1038 / 35328 words = 2.94 % |
| 主机单测 | 20 cases / 0 failures |

## 上板实证结论（2026-10-17，diag15 + DSS）

固件行为与设计**完全一致**，证据链见 `harness/tests/on-target/records/hil-2026-10-17-led-chaser.md`
与 `harness/docs/adr/0002-C28x字地址模型与GPIO实证.md`：

| 实证项 | 结论 |
|---|---|
| flash 常量表 | `led_hw_channels[]` = pin 20/22（低电平点亮），符号断点 diff 证实 |
| `GPIO_writePin` 落点 | 只写 GPASET(0x7F02)/GPACLEAR(0x7F04)，与 driverlib 结构体偏移一一相等 |
| GPADAT_R 终态 | LED4 亮 / LED5 灭，与流水灯图样一致 |
| GPADAT 低字翻转 | 悬空引脚噪声（上拉默认关闭），**非固件写入** |
| GPBDAT 低字变化 | JTAG 引脚（GPIO35-38）扫描活动，非固件写入 |

自动 HIL（`run-hil.ps1`）当前状态：**fail（测试侧 bug 已修，重跑待 XDS110 重连）**——
H5 假 FAIL 根因是 C28x 字地址模型下 16 位 `readWord(0,0x7F00)` 读不到 LED 所在的高字 0x7F01，
`hil_test.js` 已改为 32 位双字拼接 + GPADAT_R 交叉校验。

上板验证步骤见 `harness/tests/on-target/README.md`。
