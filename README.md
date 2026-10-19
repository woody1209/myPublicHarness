# f280039c-harness — AI 生产软件的嵌入式 Harness 环境（TI C2000）

面向 **TI LAUNCHXL-F280039C 开发板（TMS320F280039CPT / C28x + CLA @120MHz，
384KB Flash / 69KB SRAM，板载 XDS110 调试器）** 的固件生产环境。

本仓库是继 `C3M3-harness`（Infineon PSOC Control C3M3）之后的同构移植：为 AI 代理
（以及人类工程师）提供**确定性、可验证、可重复**的固件生产流水线——
环境自描述、脚本化构建与烧录、主机侧单元测试、CI 模板、Spec 驱动开发。

> 结构、契约与工作流对齐 `C3M3-harness`（**只读参考，本环境不修改其任何文件**）：
> 同名 `harness/schemas/*.schema.json` 契约、`specs/<id>/` 三件套 + `trace.json`、
> `spec.ps1 check` 收口、`projects/<proj>/state.json` 状态快照、"rule → agent → rule" 三段式。

## 目录结构

```
f280039c-harness/
├── README.md                    ← 本文件
├── AGENTS.md                    ← AI 代理生产规范（必读）
├── .gitignore
├── harness/                     ← 通用环境（跨项目复用）
│   ├── env/                     ← 环境快照 manifest.json + 工程配置示例
│   ├── schemas/                 ← 契约（spec / trace / state / project.config / manifest），每份都有脚本执行
│   ├── tools/                   ← verify.ps1（聚合 DoD 门）/ doctor / ccs / static-check / run-host-tests / run-hil / spec
│   │   └── lib/                 ← ToolDetect.ps1（cmake / 主机编译器探测）+ JsonSchema.ps1（契约校验子集）
│   ├── docs/                    ← 工作流总览 / 环境总览 / AI 生产规范 / 编码单测 / 烧录调试 / CI / Spec 驱动 / 两份符合性审计
│   │   └── adr/                 ← 架构决策记录（0001-0009）
│   ├── templates/ci/            ← GitHub Actions 模板
│   └── tests/                   ← 主机侧单测（cmake + Unity）+ 目标机自检清单
├── specs/                       ← 特性规格（kiro 风格三件套 + 机读契约，见 specs/README.md）
│   ├── _template/               ← 模板
│   └── 0001-led-chaser/         ← 流水灯（首个完整范例）
├── targets/
│   └── F280039C.json            ← 芯片规格卡（harness 元数据）
└── projects/
    └── launchxl-f280039c-led-chaser/   ← 参考工程（流水灯 + 上电自检 + SCI 日志）
        ├── project.config.json  ← 工程级配置（契约 harness/schemas/project.config.schema.json）
        └── state.json           ← 运行时状态快照（契约 harness/schemas/state.schema.json）
```

## 快速开始

```powershell
# 0) DoD 聚合门：环境→静态检查→主机单测→spec→状态快照→构建→体积→体积增量
.\harness\tools\verify.ps1                # 固件行为改动必须加 -Hil（板上真跑）
#    退出码即结论：VERIFY OK=0 / VERIFY FAIL 或 VERIFY INCOMPLETE=1（跳过不等于通过，ADR 0009）

# 1) 环境自检（生成 harness/env/manifest.json 并按契约自校验）—— 单腿调试时的第一步
.\harness\tools\doctor.ps1

# 2) 静态检查门禁（规则 linter + project.config 契约 + 文档路线 + cl /analyze）
.\harness\tools\static-check.ps1

# 3) 构建参考工程（默认 CPU1_FLASH）
.\harness\tools\ccs.ps1 build -Project projects\launchxl-f280039c-led-chaser

# 4) 查看产物体积（单位：C28x 字，预算来自 targets/F280039C.json）
.\harness\tools\ccs.ps1 size -Project projects\launchxl-f280039c-led-chaser

# 5) 烧录（先 probe 确认在位；需 XDS110 已连接且开发板上电）
.\harness\tools\ccs.ps1 probe
.\harness\tools\ccs.ps1 program -Project projects\launchxl-f280039c-led-chaser -Yes

# 6) 主机侧单元测试
.\harness\tools\run-host-tests.ps1

# 7) 硬件在环 HIL（需 XDS110 在线；DSS H1-H6 检查 + SCI 串口捕获 + 证据记录）
.\harness\tools\run-hil.ps1            # 串口窗口默认 90s，覆盖 DSS 启动+烧录+H6 重引导

# 8) Spec 驱动开发（非平凡改动先写 spec，见 harness/docs/70-Spec驱动开发.md）
.\harness\tools\spec.ps1 list
.\harness\tools\spec.ps1 new -Id 0002-xxx -Title "特性名"
.\harness\tools\spec.ps1 check
.\harness\tools\spec.ps1 trace -Id 0001-led-chaser
```

## 本机已验证环境

| 项 | 值 |
|---|---|
| Code Composer Studio | 21.0.1（`D:\ti\ccs2101\ccs`，含 eclipse / theia） |
| C28x 编译器 | `ti-cgt-c2000_25.11.1.LTS`（cl2000 25.11.1） |
| C2000Ware | `D:\ti\ccs2101\C2000Ware_26_01_00_00` |
| SysConfig CLI | 1.28.1（`ccs\utils\sysconfig_1.28.1\sysconfig_cli.bat`，可选） |
| 构建 make | `ccs\utils\bin\gmake.exe` |
| 烧录 | `ccs\ccs_base\scripting\examples\loadti\loadti.bat`（DSS） |
| 探针工具 | `ccs\ccs_base\common\uscif\xds110\xdsdfu.exe`（固件 3.0.0.43） |
| 主机侧工具链 | 本机为 **MSVC `cl.exe`**（`manifest.tools.host_cc=msvc`）；CMake/MinGW 未安装，`run-host-tests.ps1` 自动走离线回退路径 |
| 参考工程 | `projects/launchxl-f280039c-led-chaser`，CPU1_FLASH 构建通过 |
| 主机侧单测 | `harness/tests/host/modules.json` 声明的三个纯逻辑模块（led_pattern / fmt / self_test）用例全通过；模块与用例清单的双向一致由 `static-check.ps1` 强制 |
| 硬件在环（HIL） | `run-hil.ps1`：H1 JTAG / H2 flash / H3 自检 / H4 主循环 / H5 流水灯 / H6 留板独立运行。**2026-10-18 真板 `verify.ps1 -Hil` → VERIFY OK（9 腿全 PASS）**，HIL 侧 `HIL RESULT: PASS`（H5 节拍 mean 195.0ms / 23 样本 / 12 跳变，同日四轮 195.0–197.0ms（散布 1.0 %）全落在 ±25 % 判据内，含 banner 与 12 行心跳串口证据，记录 `harness/tests/on-target/records/hil-2026-10-18-led-chaser.md`）；此前 H5 假 FAIL 的三类测试侧原因（16 位读、锁存判读、halt 采样）已全部修入 `hil_test.js` |
| 外设时钟门控 | F28003x 复位后 PCLKCRn 全关，被门控外设的**写会被静默丢弃**：SCI 必须 `SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_SCIA)`（ADR 0005，HIL H3 有守卫） |
| 仿真启动模式 | 探针在线时 `target.reset()` 不会交接给 flash（RESC.DCON→Emulation Boot）；H6 用 `PC=code_start` 等价冷启动，断电重启须人工（ADR 0006） |
| GPIO 寄存器布局 | 字地址实证表见 `harness/docs/40-烧录与调试手册.md`：GPADAT@0x7F00（LED 位在**高字 0x7F01**）、GPADAT_R@0x7F80 |
| C28x 地址模型 | 全链路 16 位**字地址**（ADR 0002 五方实证）；driverlib GPIO 寻址实证无罪 |
| 固件启动顺序 | `.TI.ramfunc` 重定位 → 禁看门狗 → PIE 初始化 → 时钟 → 外设（ADR 0003，docs/40"固件启动顺序"节） |
| loadti 烧录 | Makefile 链接带 `--args=0x200` 定义 `__c_args__`（ADR 0004，修复 loadti 4011） |

## C28x 字地址模型（本仓库第一条硬事实）

**所有地址都是 16 位字地址**：链接器 `.cmd` origin、map 文件的地址与 size、反汇编、
DSS `readWord`、`PC`/`XARn` 完全一致；`sizeof(uint32_t)=2 字`，`*+XARn[N]` 位移为 N 字，
`memory.readWord()` 只返回一个 16 位字（32 位寄存器要读两个字拼接）。
证据链与 GPIO 寄存器真实布局（字地址表）见 `harness/docs/40-烧录与调试手册.md`，决策记录见
`harness/docs/adr/0002-C28x字地址模型与GPIO实证.md`。

## 目标板关键硬件事实（来源：TI SPRUJ31 用户指南 + 器件数据手册）

| 项 | 值 |
|---|---|
| 用户 LED | LED4（红）= **GPIO20**，LED5（绿）= **GPIO22** |
| LED 极性 | **低电平点亮**（经 SN74LVC2G07DBVR 驱动）；GPIO 写 0 = 亮，写 1 = 灭 |
| 调试接口 | 板载 XDS110，**cJTAG (1149.7) 2-pin advanced** —— 见 `harness/docs/40-烧录与调试手册.md` |
| 仿真串口 | XDS110 虚拟 COM：SCIA TX=GPIO29、RX=GPIO28（115200-8N1） |
| 内部振荡器 | INTOSC1/INTOSC2 = 10 MHz（默认时钟源，见 ADR 0001） |
| 系统时钟 | 120 MHz SYSCLK / LSPCLK = 30 MHz |
| GPIO 数据寄存器 | GPADAT（32 位）@字 0x7F00：低字 0x7F00=GPIO0-15、**高字 0x7F01=GPIO16-31**（LED4=bit4、LED5=bit6）；GPADAT_R 引脚读回 @字 0x7F80。完整表见 `harness/docs/40-烧录与调试手册.md` |
| 上拉默认 | C2000 内部上拉**默认关闭**（GPAPUD 复位全 1）；未配置引脚悬空，GPADAT 低字有噪声，非固件行为 |

## 文档索引

- [AGENTS.md](AGENTS.md) — AI 代理在本仓库的工作规则（必读）
- [harness/docs/00-harness工作流总览.md](harness/docs/00-harness工作流总览.md) — **入口文档**：换板/换 MCU 的 AI coding 开展流程 + 新功能变化的 harness 体现路径
- [harness/docs/diagrams/](harness/docs/diagrams/) — 图示（.drawio 可编辑）：环境配置总览 / 场景A 换板工作流 / 场景B 新功能工作流
- [harness/docs/10-环境总览.md](harness/docs/10-环境总览.md)
- [harness/docs/20-AI生产规范.md](harness/docs/20-AI生产规范.md)
- [harness/docs/30-编码与单测规范.md](harness/docs/30-编码与单测规范.md)
- [harness/docs/40-烧录与调试手册.md](harness/docs/40-烧录与调试手册.md)
- [harness/docs/50-CI与版本管理.md](harness/docs/50-CI与版本管理.md)
- [harness/docs/70-Spec驱动开发.md](harness/docs/70-Spec驱动开发.md)
- [harness/docs/80-harness闭环符合性审计.md](harness/docs/80-harness闭环符合性审计.md) — **架构师审计**：三柱闭环（SPEC/TDD/harness 控制）符合性结论与缺口
- [harness/docs/81-AIDLC符合性审计.md](harness/docs/81-AIDLC符合性审计.md) — **AIDLC（AI-DLC/SDD）符合性审计**：五阶段与十诫映射、证据完整性缺陷修复、遗留补强
- [harness/docs/adr/](harness/docs/adr/) — 架构决策记录：0001 平台选型 / 0002 字地址模型 / 0003 ramfunc 重定位与启动顺序 / 0004 loadti 与 `__c_args__` / 0005 外设时钟门控与 CPUTIMER 时基 / 0006 仿真启动模式（DCON）与 H6 冷启动 / 0007 头文件依赖与陈旧镜像 / 0008 主机单测门禁的假 PASS
- [specs/README.md](specs/README.md) — spec 工作区规则速查
- [projects/launchxl-f280039c-led-chaser/README.md](projects/launchxl-f280039c-led-chaser/README.md)
