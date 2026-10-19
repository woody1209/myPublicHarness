# ADR 0003 · `.TI.ramfunc` 重定位与 C28x 固件启动顺序

- 状态：已接受
- 日期：2026-10-17
- 决策者：f280039c-harness 项目

## 背景

参考工程初期症状：上电/HIL 加载后**启动几毫秒即死**——无串口输出、无 LED 活动，
HIL 各项全挂。排查链（diag8 → diag11）：

1. 停机 PC 反复停在 `board_clock_init`（0x82A3A），RESC=0（排除真实复位事件）；
2. 断点差分显示 `GPIO_writePin` 从未命中（固件根本没走到外设初始化）；
3. diag11 在同一会话内抓到 ITRAP 现场：故障 PC = `0x3FEEF8`（boot ROM 向量区），
   且 RAMLS0（0x8000）内容为全 0。

根因：driverlib 的 `SysCtl_delay()` 以**内联汇编**编入 `.TI.ramfunc` 段
（时钟/Flash 等待态重配期间必须身在 RAM）。flash 链接脚本把该段拆为
`LOAD = FLASH_BANK0_SEC1, RUN = RAMLS0 (= 0x8000)`，而 C 运行库 `_c_int00`
**只清零 `.bss`、展开 `.cinit`，不做 LOAD→RUN 段拷贝**。`SysCtl_setClock()`
首次调用 `SysCtl_delay()` 即跳进空 RAMLS0，`0x0000` 被当指令执行 → ITRAP；
此刻 PIE 未初始化，陷阱向量取自 boot ROM，`CPU1BROM_itrapISR` 使能看门狗
等待复位——表现为"静默死亡"。

## 决策

1. **`main()` 入口顺序固化为硬约束**（新工程照抄参考工程 `main.c`）：
   0) `.TI.ramfunc` 重定位 + `board_ramfunc_ready()` 逐字校验 →
   0b) `SysCtl_disableWatchdog()` →
   0c) `Interrupt_initModule()` + `Interrupt_initVectorTable()` →
   1) 时钟（PLL 120MHz + LSPCLK/4 + 回读校验）→ 2) 外设（GPIO/SCI）。
2. **`board_clock_init()` 内防御性再重定位一次**：任何调用方都不可能踩雷，
   时钟模块自给自足。
3. **PIE 初始化列为必选项**：本参考工程无 ISR（CPUTimer 轮询），但 PIE 禁用时
   任何杂散陷阱都会进 boot ROM 的 ITRAP handler（使能看门狗复位）；
   打开 PIE + driverlib 默认向量表让故障保持本地可调试。
4. **知识落盘**：完整证据与判读口诀写入 `harness/docs/40-烧录与调试手册.md`
   "固件启动顺序"节；`harness/docs/30-编码与单测规范.md` 编码规范增加启动顺序硬约束条目。

## 证据链

| # | 证据 | 结论 |
|---|---|---|
| 1 | diag10：ROM `CPU1BROM_itrapAddress`（0x1E）读出故障 PC | ITRAP 真实发生 |
| 2 | diag11：故障 PC = 0x3FEEF8（ROM 向量区），RAMLS0 全 0 | 跳进了空 RAM 执行 |
| 3 | 停机 PC 停在 board_clock_init（0x82A3A）+ RESC=0 | 死在时钟初始化路径，非复位循环 |
| 4 | flash 链接脚本 `.TI.ramfunc` LOAD/RUN 拆分 + map `Ramfuncs*` 符号 | 段确需运行时拷贝 |
| 5 | 修复后 diag12/13/15：PC 进入应用主循环、GPADAT_R 终态正确、LED 节拍正常 | 修复有效，无回归 |

## 备选与理由

| 备选 | 落选原因 |
|---|---|
| 把 `.TI.ramfunc` 整体改放 RAM（去掉 LOAD/RUN 拆分） | flash 构建的加载镜像必须在 flash；拆分是 TI 官方模型，正确做法是运行时拷贝而非改布局 |
| 在 `_c_int00`/启动代码里统一做段拷贝 | 改动 C 运行启动路径影响面大；参考工程在 `main()` 入口显式拷贝 + ready 校验，意图更直白 |
| 不初始化 PIE（本工程无 ISR） | 杂散陷阱会进 ROM handler 触发看门狗复位，故障不可本地化；两行初始化成本极低 |
| 只修不校验（relocate 后不读回比对） | 链接脚本改动丢失 `Ramfuncs*` 符号或烧录旧镜像时会静默失败；`board_ramfunc_ready()` 把这类回归变成可见 FAIL |

## 后果

- "启动即死"类故障有了确定性判读路径：PC 停时钟初始化 → 先查 ramfunc 重定位。
- 所有新 C28x 工程继承同一启动顺序（AGENTS.md §4 / docs/30 第 8 条强制）。
- 成本：RAMLS0 需容纳 `.TI.ramfunc` RUN 段（当前镜像数百字节，RAMLS0 2KB 充裕）。
- 触发复审：若更换 driverlib 大版本导致 `SysCtl_delay` 段归属变化，或改用
  CLA/双核工程，需重跑"空 RAM 跳转"对账并更新本 ADR。
