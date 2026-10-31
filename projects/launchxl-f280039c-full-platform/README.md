# projects/launchxl-f280039c-full-platform — 全外设并存平台工程

LAUNCHXL-F280039C 上 F28003x **全部处理器子系统并存**的平台工程（spec
`specs/0002-full-platform/`）：统一调度器驱动、健康监视覆盖、全回环零外部硬件。
参考工程 `launchxl-f280039c-led-chaser` 保持基线不动；本工程骨架拷自它并扩展。

## 当前里程碑状态

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M1 | 工程骨架 + scheduler + health + 扩展自检 + harness 多工程化 | **完成（2026-10-24）** |
| M2 | DCC / ADC / DAC+CMPSS / ePWM / eCAP / eQEP | **完成（2026-10-25，HIL H1-H7 PASS，eCAP 9999/19996/49979 Hz 精确；DAC 回环通道发现与 eQEP 计数 bring-up 开放项）** |
| M3 | SPI / I2C / SCIB+LIN / CAN / FSI / DMA | **平台稳定 + SCIB 回环板上验证（2026-10-25 深夜会话）。三大 bring-up 根因已修并落档：① PIE 侧 ADC INT1 使能 = 复位源（已去除，DMA 用原始信号）；② DMA PERINTSEL=0 是保留值（armed 通道被卡死源反复触发走 RAM）→ 软件通道改 XINT5；③ CAN 时钟源默认 XTAL（板无晶振）→ IF1CMD.BUSY 死等 → 显式 SYSCLK。SPI/I2C/LIN/CAN/FSI/eQEP/DAC 回环 bring-up 仍开放（REQ-011..016 pending，计数器在 m3 健康行观测）** |
| M4 | CLA / VCRC+TMU+AES+BGCRC / CLB / 看门狗 / EPG | **代码集成（2026-10-30）：accel(BGCRC 黄金窗/AES ECB 往返/TMU vs math_ref) + clb GPREG 证明 + CLA（MemCfg LS1 程序主 + 消息 RAM + ADR-0003 类 load 拷贝）+ 看门狗（bring-up 开放：/64 窗口 vs 遍时长交互，RESC=0x4 证据）+ EPG 寄存器级。任务在回环 bring-up 期间门控** |
| M5 | HIL H7–H11 / 30min soak / 故障注入 / 收口 | 待实现 |

## 构建 / 验证

```powershell
.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-full-platform            # 默认 CPU1_FLASH
.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-full-platform -Config CPU1_RAM
.\harness\tools\ccs.ps1 size  -Project projects\launchxl-f280039c-full-platform
.\harness\tools\run-host-tests.ps1                                                          # 双工程 42 用例
.\harness\tools\static-check.ps1                                                            # 双工程清单互锁 + /analyze
```

## M1 行为约定

| 项 | 值 |
|---|---|
| 启动顺序 | ADR 0003 硬约束：ramfunc 重定位+校验 → 禁看门狗 → PIE → 时钟 → 外设（M4 再启用窗口看门狗） |
| 调度 | `source/scheduler.c`（纯逻辑）：任务描述符表，1ms CPUTimer0 节拍轮询，周期/错峰/迟到统计，无追赶积压 |
| 健康 | `source/health.c`（纯逻辑）：心跳/错误/连续错误阈值；MUST 子系统连续 3 错 → fail-safe（双灯 100ms 同闪） |
| M1 任务 | tbase（1ms 时基轮询）/ ledch（200ms LED 流水，保留 H5 节拍观测）/ hline（5s 聚合健康行） |
| 健康行 | `health: up=<s> map=0x.. err=<n> hb=<n> step=<n>, mode=<m>`（SCIA 115200-8N1，每 5s） |
| 自检 | 扩展自检（REQ-020）：pattern/FNV + fmt 参考向量 + scheduler due-bucket 参考向量 + health 阈值不变量，主机与目标共用同一份 `self_test.c` |
| 引脚 | `source/board_pins.h` 全量矩阵（design.md §6）：GPIO35-38 JTAG 红线不动，GPIO20/22/28/29 职能不变 |

## M1 基线（2026-10-24）

| 项 | 值 |
|---|---|
| 构建配置 | CPU1_FLASH + CPU1_RAM 双通过 |
| `.text` | 4815 words（.text.1 4096 + .text.2 719） |
| Flash 占用 | 6187 / 196608 words = 3.15 % |
| RAM 占用 | 1482 / 35328 words = 4.19 % |
| 主机单测 | 双工程 50 cases / 0 failures（led-chaser 20 + full-platform 30，M2 新增 adc_stat/math_ref） |
| M2 基线 | .text 10693 words（+5878 vs M1），Flash 6.93%，RAM 4.80% |
| M3 基线 | .text 14450 words，Flash 9.53%，RAM 5.11%；主机单测 56 cases |
| harness 变更 | modules.json v2（projects[] 分组）、CMakeLists/run-host-tests/static-check 多工程化，led-chaser 用例零语义变化 |

子系统任务将在 M2–M4 逐批注册进调度表；每批收口要求双工程构建 + 双 runner 主机单测 +
static-check 0 violations + led-chaser 回归全绿。
