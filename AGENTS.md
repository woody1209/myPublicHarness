# AGENTS.md — AI 代理固件生产规范（本仓库强制规则）

> 对象：在本仓库产出、修改、测试固件的所有 AI 代理与自动化代理。
> 人类工程师同样适用。规则按优先级排序，冲突时按靠前规则执行。
>
> 本环境参照 `C3M3-harness`（Infineon PSOC C3M3）移植，结构与工作流保持一致；
> **`C3M3-harness` 目录为只读参考，严禁修改其中任何文件。**

## 1. 环境发现（每次会话第一步）

0. 读流程地图 `harness/docs/00-harness工作流总览.md`：换板/换 MCU 怎么做、新功能怎么流转，
   本文是其强制版。
1. 运行 `.\harness\tools\doctor.ps1`，读取 `harness/env/manifest.json`。
   manifest 未生成或与本机不符时，**先修 doctor 再继续**，禁止凭猜测假设 CCS / 编译器 / C2000Ware 路径。
2. 所有构建/烧录命令**必须**经 `.\harness\tools\ccs.ps1` 包装器执行；
   禁止直接调裸 `cl2000` / `gmake`（工具路径与版本由包装器统一注入）。
3. SDK 版本以 `harness/env/manifest.json` 记录的 C2000Ware 为准；
   升级 C2000Ware / 编译器必须更新 manifest 并在提交信息与 ADR 中说明。

## 2. 构建与验证（Definition of Done）

**一条命令跑完下表全部软件门禁**：`.\harness\tools\verify.ps1`；固件行为改动必须加 `-Hil`（含 #9 上板）。
它逐条执行门禁、原样上报每条**自己的**退出码与量化判据，任何一条没能真正计数都不算 PASS；
`-Skip` 或省略 `-Hil` 时矩阵打印 `VERIFY INCOMPLETE` 并退出 1——跳过不等于通过（ADR 0009）。

覆盖范围要说清，免得把"没跑"读成"过了"：`verify.ps1` 的腿 = #0 #1 #1b #2 #2b #3 #4 #5 #6 #9(-Hil) #10，
其中 **#1b 代码审查**（`review.ps1`，ERROR 级 0 findings）与 **#2b 软测 SIL**（`run-sil-tests.ps1`，
时间序列性质）是 2026-10-31 补的第九、十条腿——测试金字塔从此是三层：**单测(#3) → SIL(#2b) → HIL(#9)**，
#11 由 #0 的 trace 证据路径核对机器承担。**#7 连接确认、#8 loadti 烧录路径、#12 文档回写不在聚合门里**：
`-Hil` 的 HIL 腿自带探针预检与 DSS 烧写，但它**不经过** `ccs.ps1 program`，所以 #7/#8 要按 §3
报告 ccxml／MPN／`.out`／加载结果后单独执行；#12 只能人工评审（`DOC-DEAD-ROUTE` 只保证路线可走，不保证内容已更新）。

| # | 门禁 | 可执行判据 | 单独运行 |
|---|---|---|---|
| 0 | 规格先行 | 非平凡改动（新模块/新外设/协议/时钟策略/跨文件重构/链接脚本或启动代码变更）已先写 `specs/<id>/` 三件套并经用户确认（§7）；每条 `REQ-XXX` 有 EARS 验收句 + 可执行验收；`spec.json`/`trace.json` 过 schema；trace 里每条证据路径**真实存在**；`status=verified` 与"延后/未执行"表述冲突时必须给 `waiver_reason` | `.\harness\tools\spec.ps1 check` → `CHECK OK`，退出 0 |
| 1 | 环境发现 | `harness/env/manifest.json` 与本机一致；CCS / CGT / C2000Ware / gmake / loadti / xdsdfu 全部解析 | `.\harness\tools\doctor.ps1` → `environment OK` |
| 1b | 代码审查 | **ERROR 级 0 findings**：`REVIEW-TODO-UNTRACKED`（TODO/FIXME 必须带 spec/ADR/issue 引用）、`REVIEW-MAGIC-REG-ADDR`（禁止把字面地址传给 `HWREG*`，字寻址下这是静默 bug 类）、`REVIEW-GUARD-MISSING`（头文件守卫）、`REVIEW-LONG-FILE`（>1000 行）。**WARN 级不阻断**：`REVIEW-SPEC-UNTRACED`（追溯债务，记入审计待办）。门必须能红——已用注入探针验证过 REVIEW FAILED / exit 1 | `.\harness\tools\review.ps1 [-Project <proj>]` → `REVIEW OK (0 findings)` |
| 2 | 静态检查 | 规则 linter **0 findings**（JTAG 引脚复用、裸引脚号、裸 `sprintf`/`stdio`/动态内存、driverlib 分层、裸工具链调用）＋ `cl /W4 /analyze` 对主机可编译源 0 诊断 ＋ 主机测试模块清单与 `CMakeLists.txt`/`test_runner.c` **双向一致** ＋ `project.config.json` **过契约** ＋ 全部文档路线**可走**（`DOC-DEAD-ROUTE` 0 条） | `.\harness\tools\static-check.ps1` → `STATIC CHECK OK (0 violations)` |
| 2b | 软测 SIL | **时间序列性质**（不是单返回值）：`cases > 0` 且 `failures == 0`，且输出 `SIL RESULT: PASS`。清单 `harness/tests/sil/modules.json`。SIL 跑真实模块于仿真主循环内：调度器节拍/相位错峰/卡顿无 backlog、健康监视 MUST 阈值锁存与恢复、SVPWM 整周波的定心与线电压不变性 | `.\harness\tools\run-sil-tests.ps1` → `SIL RESULT: PASS` |
| 3 | 单元测试 | `cases > 0` **且** `failures == 0`（跑了 0 个用例不算通过） | `.\harness\tools\run-host-tests.ps1` |
| 4 | 固件构建 | 目标 `CONFIG` 链接成功，退出码 0 | `.\harness\tools\ccs.ps1 build -Project <proj>` |
| 5 | 体积预算 | 按 **C28x 字**计量（1 字 = 2 字节，ADR 0002）：FLASH ≤ 90 %／RAM ≤ 90 %，预算读自 `targets/F280039C.json` | `.\harness\tools\ccs.ps1 size -Project <proj>` → `size gate PASS` |
| 6 | 体积增量登记 | `.text` 与 `projects/<proj>/state.json` 的 `build.size.text_words` 基线之差 ≤ 1024 字（2 KB）；超过就得先在 state.json 与提交信息里说明来源 | `verify.ps1` 的 `size-delta` leg |
| 7 | 连接确认 | `xdsdfu -e` 枚举到 ≥1 台 XDS110（**#8 的硬前置**，见 §3） | `.\harness\tools\ccs.ps1 probe` |
| 8 | 烧录路径 | §3 报告 ccxml／MPN／`.out`／加载结果之后，loadti 退出 0；镜像须含 `__c_args__`（ADR 0004）；404／4011 类错误查 `harness/docs/40-烧录与调试手册.md`「常见故障」 | `.\harness\tools\ccs.ps1 program -Project <proj> -Yes` |
| 9 | 上板 HIL | `HIL RESULT: PASS` 且 H1–H6 **每条** PASS；H5 节拍均值落在 200 ms ±25 %（`CADENCE_TOL = 0.25`）且观测到 ≥8 次 LED 跳变；`g_self_test_code==0` 必须与 H5 组合判读；退出码 3 = 探针不在 = UNAVAILABLE，**禁止**读成通过 | `.\harness\tools\run-hil.ps1` |
| 10 | 状态快照 | `projects/<proj>/state.json` 通过 `harness/schemas/state.schema.json`；size 字段一律带 `*_words` 单位 | `verify.ps1` 的 `state-schema` leg |
| 11 | 证据留存 | HIL／故障注入记录落在 `harness/tests/on-target/records/`，并被 `trace.json` 的 `tests`／`verification` 引用（由 #0 机器核对） | 见 `harness/tests/on-target/README.md` |
| 12 | 文档回写 | 新增/修改行为写进 `projects/<proj>/README.md` 或对应 `harness/docs/`；技术决策写 `harness/docs/adr/NNNN-*.md` | 人工 + PR 评审 |

纯逻辑模块必须落在 `projects/<proj>/source/` 且**只依赖 C 标准库 + 本仓库头文件**，保证主机侧可编译测试；
硬件相关代码必须隔离在 `*_hw.c` / `board_*.c` 或接口函数后面（这条由 #2 的 `LAY-*` 规则机器检查）。

写/改 `harness/tools/dss/*.js` 前必读 `harness/docs/40-烧录与调试手册.md`「DSS / HIL 调试经验」节：
`memory.readWord()` 只返回一个 16 位字，LED 位在 GPADAT **高字 0x7F01**，未用引脚悬空会闹噪声——
这三条是本项目踩出来的假 FAIL/假 PASS 根因，文档是其唯一所有者，本表不重复。

## 3. 烧录与调试安全（不可绕过的红线）

- `ccs.ps1 erase` 是**破坏性操作**：执行前必须显式 `-Confirm` 并取得用户明确确认
  （禁止代理自行擦片）。
- **烧录（`program`／`qprogram`）前必须先跑 `ccs.ps1 probe` 并核对枚举结果**：判据是输出
  `Found N device`（N ≥ 1），并把设备名／序列号报告给用户再烧。
  枚举不到（`Found 0 devices`）或设备与 `targets/F280039C.json` 不符时**禁止烧录**，
  也**禁止**把这种状态写成"验证通过"。probe 是 DoD #7，是 #8 的硬前置。
- 烧录前后必须向用户报告：ccxml 路径、目标 MPN、`.out` 路径、加载结果。
- `ccs.ps1 program` 仅对当前 `-Project` 生效；一次只烧一个工程。
- 不得把 GPIO35–38（JTAG/TMS/TDO/TCK）复用为 GPIO —— 会导致 JTAG 永久失效。

## 4. 代码规范（摘要，详见 `harness/docs/30-编码与单测规范.md`）

- C11；禁止 VLA；禁止动态内存用于固定工作负载。
- 所有模块：头文件 + `static` 内聚 + 显式返回码。
- 日志统一走 `sci_log`（XDS110 虚拟 COM，115200-8N1）；禁止裸 `sprintf` 到定长缓冲区。
- 时钟/系统配置集中在 `projects/<proj>/source/board_clock.c`，禁止在业务代码里散落硬编码寄存器值
  （driverlib 内部除外）。
- **C28x 字地址模型**（ADR 0002 / `harness/docs/40-烧录与调试手册.md`）：所有地址——`.cmd` origin、map 文件的地址**与
  size**、反汇编、DSS `readWord`、`PC`/`XARn`——均为 16 位字；`sizeof(uint32_t)=2 字`，
  `*+XARn[N]` 位移为 N 字。遇到地址/大小"矛盾"先按字模型对账，
  **禁止**据此怀疑 driverlib 寻址——2026-10-17 已五方实证其完全正确。
- **C28x 没有 8 位类型**：`char`=16 位（`CHAR_BIT=16`），`uint8_t`/`int8_t` 不存在，
  字节级字段一律 `uint16_t`；driverlib `--define=DEBUG` 的断言需要应用提供 `__error__`
  （本仓库范例：`projects/<proj>/source/board_error.c`）。
- **启动顺序硬约束**（ADR 0003 / `harness/docs/40-烧录与调试手册.md`「固件启动顺序」节）：`main()` 首动作必须
  `.TI.ramfunc` 重定位 + ready 校验，其后才允许任何 `SysCtl_*`/延时；随后禁看门狗 →
  PIE 初始化 → 时钟 → 外设。违反=启动即死（ITRAP→看门狗复位，无日志无灯）。
- **链接必须带 `--args=<n>`**（ADR 0004）：loadti（`ccs.ps1 program`）以
  `loadProgram(file, argv)` 加载，镜像未定义 `__c_args__` 时报错误码 4011；
  新工程拷 Makefile 时保留 `--args=0x200`。改 LDFLAGS 后注意 `.out` 重链
  （Makefile 已列入 `.out` 依赖；必要时 `ccs.ps1 rebuild`）。
- **GPIO 引脚只能在 `targets/F280039C.json` 与 `projects/<proj>/source/board_pins.h` 中出现一次**，
  业务代码引用符号名不许出现裸引脚号。
- **改头文件后必须确认产物真的更新了**（ADR 0007）：gmake 打印
  `'C2000 Compiler - building file: ...'` **不等于** `.obj`/`.out` 被重写。
  判据只认两样 —— `.obj`/`.out` 的时间戳，以及 `dis2000 <image>` 反汇编里的常量。
  产物不更新时用 `ccs.ps1 rebuild`（clean + all）兜底。陈旧镜像会制造假 PASS。
- **PowerShell 门禁脚本的返回值纪律**（ADR 0008）：函数若要返回布尔，内部**禁止**
  用 `Write-Output` 输出信息，也**禁止**让原生命令（如 `& cl.exe`）的输出漏到输出流 ——
  它们会全部变成返回值，使调用方的 `if (...)` 因"非空数组恒为真"而永远成立。
  提示一律用 `Write-Host`，原生调用先捕获再转发。

## 5. 知识落盘（强制）

**记录系统在本仓库之内**——写进下面的所有者文件，不许落到仓库外的私人目录（§6 第 3 条优先）。
每条落盘都要能被后人按路径找到，且能被 DoD #12 的证据核对引用。

| 产出物 | 唯一所有者（路径） |
|---|---|
| 本机工具链路径／版本／环境踩坑备忘 | `harness/docs/10-环境总览.md`「环境修复备忘（踩坑记录）」节；机器可读部分由 `harness/env/manifest.json` 生成（doctor 产物，不手写） |
| 编码与单测规范细则 | `harness/docs/30-编码与单测规范.md` |
| 烧录／调试／DSS 判读经验、故障表 | `harness/docs/40-烧录与调试手册.md` |
| 技术决策（为什么这么选、放弃了什么） | `harness/docs/adr/NNNN-*.md`（模板 `harness/docs/adr/template.md`，范例 0001–0008） |
| 需求／设计／任务／追溯 | `specs/<id>/`（`spec.ps1 new` 生成骨架） |
| 上板与故障注入证据 | `harness/tests/on-target/records/`（HIL 记录由 `run-hil.ps1` 自动写） |
| 对外行为与体积／引脚事实 | `projects/<proj>/README.md` + `projects/<proj>/state.json`（后者受 `state.schema.json` 约束） |
| 门禁链与符合性审计结论 | `harness/docs/80-harness闭环符合性审计.md`（motor-pwm 前的快照）、`harness/docs/81-AIDLC符合性审计.md`（同上）、`harness/docs/82-工程完备性审计.md`（覆盖两个工程 + 基线对比，最新） |
| 变更记录（来源/改动/验证） | `CHANGELOG.md`（2026-10-31 建立；此前仓库无变更记录） |
| 学习材料（电机控制时序/原理演示/MCU 资源时序） | `docs/learning-site/`（`index.html` 静态理论 / `dynamic.html` 动态演示 / `mcu.html` MCU 时序；算法核 `docs/learning-site/js/core.js` 与 firmware 同算法） |

## 6. 禁止事项

- 禁止修改参考工程 `C3M3-harness/**`（只读参考）。
- 禁止把 build 产物提交入库（.gitignore 已覆盖）。
- 禁止在本仓库之外的用户目录做任何写操作。
- 禁止在未跑构建的情况下声称"完成"。
- 禁止未获用户确认就越过 spec 的 requirements/design 阶段直接写代码（见 §7）。

## 7. Spec 驱动（非平凡改动，强制）

**改动会引入新的对外行为或新的硬件依赖 → 先写 spec，再写代码。**
判定表与流程见 `specs/README.md`，完整说明见 `harness/docs/70-Spec驱动开发.md`。

1. 新建：`.\harness\tools\spec.ps1 new -Id <NNNN-slug> -Title "<名称>"`，
   在 `specs/<id>/` 填 `requirements.md`（用户故事 + EARS 验收 `REQ-XXX`）
   与 `spec.json`；**经用户确认**后把 `meta.status` 改为 `requirements-approved`。
2. 填 `design.md`（架构/接口/内存/验证策略 + 需求覆盖表），**经用户确认**后改为
   `design-approved`。**未确认不得开始写代码。**
3. 按 `tasks.md` 实现，完成一项勾选一项，并把 文件/测试/验证命令 填进 `trace.json`。
4. 收口：`.\harness\tools\spec.ps1 check` 通过（退出码 0）才算规格侧完成；
   `status=done` 要求任务全勾选且 trace 全 `verified`。
5. 实现中若需求需变：**先改 spec 并重新确认，再改代码**（spec 与代码双向一致）。
6. 每条 REQ 至少一条 EARS 验收句 + 一条可执行验收（命令或断言）；
   EARS 句必须可观测、可判定，禁止"尽量/较快"这类模糊表述。

契约文件在 `harness/schemas/`；运行时状态快照在 `projects/<proj>/state.json`
（契约 `state.schema.json`，只记指针与统计，真相是产物与命令输出）。
范例见 `specs/0001-led-chaser/`。
