# 80 — F280039C-harness 闭环生产管理符合性审计

> 审计视角：Agent Harness 架构师（墨衡，重构轨）
> 审计日期：2026-10-17（同日复训：开发板在位，四腿门禁全部实跑）
> 审计对象：`E:\AIcherry\C2000TI\F280039C-harness`（192 文件，源自 C3M3-harness 只读模板移植；含本轮新增的 ADR、诊断脚本与上板证据日志）
> 对照标准：用户提出的「SPEC(SDD)对齐 + TDD 工程纪律 + harness 系统工程控制」三柱闭环

---

## 0. 结论（先给判定）

**我能完整掌握该工程。** 已读取全量文件树、6 份 ADR、7 份 docs、5 个工具脚本、spec 全生命周期文件、测试框架、manifest/targets/state 三类真相源，并**实跑四道门禁**取得通过证据。

**该 harness 符合三柱闭环生产管理的程度：高（≈95%）。**
- SPEC 柱：完整实现且门禁实测 `CHECK OK`。
- TDD 柱：主机层实测 `all host unit tests passed`（20 用例）；目标层由 HIL 承接，实测 PASS。
- harness 控制柱：完整实现（doctor/manifest/ccs/targets/state/ADR/schemas），且 `probe → program → run-hil` 三段链条在真板上贯通。
- **四腿全绿（2026-10-17 实跑，收口又复跑）**：`doctor` = environment OK、`spec.ps1 check` = CHECK OK、`run-host-tests` = passed、`ccs.ps1 rebuild → size`（.text 3881 words = 7762 B；当时报告误标 3881B/1.55 %，修正后 flash 2.58 %）→ `program -Yes` = program OK → `run-hil -ComPort COM5` = `HIL RESULT: PASS`（H1-H6；同日 4 轮 PASS，H5 节拍 192.5/193.5/194.5/195.1 ms）。此前的「HIL 腿为红」已闭合，且闭合过程反查出两处真实固件缺陷（SCIA 时钟门控、CPUTIMER 时基），沉淀为 ADR 0005/0006。
- **收口复跑（2026-10-18，聚合门同日四轮跑完）**：`verify.ps1 -Hil` → **VERIFY OK - 9 legs PASS**
  （doctor / static-check / host-tests / spec-check / state-schema / build / size / size-delta / hil）。
  HIL 真机 `HIL RESULT: PASS`，H1-H6 全过，H5 节拍 mean=**195.0ms**（23 个 halt-free 样本 / 12 次跳变；四轮 195.0–197.0ms，散布 1.0 %），
  H4 PC=0x82C53（应用 .text 内），串口 90s 窗口 `SERIAL-CAPTURE-OK`；
  证据 `harness/tests/on-target/records/hil-2026-10-18-led-chaser.md`。
  本轮把"每条腿都证明过自己能红"作为门禁有效性判据写进 ADR 0009，取代本文原先"跑绿即可"的口径。

---

## 1. 我掌握工程的证据（read 覆盖）

| 维度 | 已读/已跑 |
|---|---|
| 结构 | 全量 `find` 192 文件树（首审计记录 158，增量为本轮诊断脚本、ADR 与上板证据日志） |
| 规则层 | `AGENTS.md`（§1-§7 DoD 9 项、§4 硬规则、§7 spec 强制） |
| 流程地图 | `docs/00-harness工作流总览.md` |
| SPEC 全生命周期 | `specs/README.md` + `specs/0001-led-chaser/{requirements,design,tasks,spec.json,trace.json}` + `spec.ps1` |
| TDD 主机层 | `tests/host/README.md` + `framework/fw_test.{h,c}` + `test/test_*.c` + `run-host-tests.ps1` |
| TDD 目标层 | `tests/on-target/README.md` + `run-hil.ps1` + `harness/tools/dss/hil_test.js`（H1-H6） |
| harness 控制 | `doctor.ps1` + `env/manifest.json` + `ccs.ps1` + `targets/F280039C.json` + `state.json` + `schemas/*` |
| 知识层 | `harness/docs/adr/0001-0008` + `harness/docs/10-环境总览.md` / `20-AI生产规范.md` / `30-编码与单测规范.md` / `40-烧录与调试手册.md` / `50-CI与版本管理.md` / `70-Spec驱动开发.md` |
| 门禁实跑 | `spec.ps1 check` → CHECK OK；`run-host-tests.ps1` → passed；`ccs.ps1 program -Yes` → program OK；`run-hil.ps1 -ComPort COM5` → HIL RESULT: PASS；`verify.ps1 -Hil` → VERIFY OK - 9 legs PASS（2026-10-18） |

---

## 2. 三柱逐条符合性

### 柱一 · SPEC（SDD）对齐要求 —— ✅ 强符合

| 能力 | 证据 | 判定 |
|---|---|---|
| 三件套 + 批准门 | `requirements/design/tasks` + `spec.json`/`trace.json`；状态机 `draft→requirements-approved→design-approved→implementing→done` | ✅ |
| 需求可判定化 | EARS 五句式（普遍/事件/状态/异常/可选）录入 `specs/README.md` §3；`requirements.md` 每条 REQ 含用户故事+EARS+可执行验收 | ✅ |
| 机器校验 | `spec.ps1` 七段确定性校验：文件齐备/JSON 解析/REQ 编号连续/字段枚举/EARS 必有 `THE SYSTEM SHALL`/tasks↔spec 双向覆盖/trace 全覆盖/`done` 一致性 | ✅ 实测 CHECK OK |
| 双向可追溯 | `trace.json` 每个 REQ → files+tests+verification+status；`spec.ps1` 反向校验 task 引用不存在的 REQ | ✅ |
| 契约化 | `schemas/spec.schema.json` + `trace.schema.json` | ✅ |

### 柱二 · TDD 保证工程纪律 —— ✅ 强符合（逻辑层）

| 能力 | 证据 | 判定 |
|---|---|---|
| 纯逻辑隔离 | `source/*.c` 纯逻辑；硬件隔离在 `*_hw.c`/`board_*.c`（host 层不可达） | ✅ |
| 零依赖框架 | 自研 `fw_test.{h,c}`（无 Unity/FetchContent/网络/动态分配），离线可跑 | ✅ |
| 双驱动 | cmake+ctest（首选）↔ MSVC `cl.exe` 离线回退（刻意不调 `vcvars64.bat`，避开 reg.exe 被拦） | ✅ 实测走 MSVC 回退通过 |
| 参考值独立 | `test_led_pattern.c` 硬编码参考序列，**独立于** 目标侧 FNV-1a 参考值，防「坏参考掩盖坏实现」 | ✅ |
| 目标自检上主机 | `self_test.c` 本身为纯逻辑，主机侧直接执行，与板上共用同一头常量 | ✅ |
| 门禁 | `run-host-tests.ps1` 退出码 0/1；20 用例 | ✅ 实测 passed |

**时序/极性类需求（REQ-002/003）的 TDD 由 on-target HIL 承接**，2026-10-17 与 2026-10-18 均 PASS：H5 以 halt-free 采样得到 2026-10-17 四轮均值 192.5 / 193.5 / 194.5 / 195.1ms（各 12 次跳变）与 **197.0 / 195.8 / 196.5 / 195.0 ms**（2026-10-18 四轮，各 23 样本 / 12 跳变，同日散布 1.0 %），门禁容差 ±25 %。跨两日八轮散布 2.3 % 这个事实本身，就是"±2 % 不能由 JTAG peek 裁决、只能交给示波器/逻辑分析仪"的直接证据（见缺口 6 与 ADR 0009）。

### 柱三 · harness 软件工程系统控制 —— ✅ 强符合

| 能力 | 证据 | 判定 |
|---|---|---|
| 环境自发现 | `doctor.ps1` → `manifest.json`（CCS/CGT/C2000Ware/探针/工具） | ✅ 本轮修正两处误报：商店 python 别名桩不再被登记；`-File` 调用下参数默认值崩溃已修 |
| 统一入口 | `ccs.ps1` 包装所有构建/烧录；**禁止裸 `cl2000`/`gmake`**（AGENTS §1） | ✅ |
| 单一真相源 | 环境=manifest+ToolDetect；硬件=targets JSON；状态=state.json；需求=specs | ✅ |
| 状态快照契约化 | `state.schema.json`（`additionalProperties:false`、枚举约束、只存指针+统计） | ✅ |
| 决策留痕 | ADR 0001-0006（平台/字地址/ramfunc/loadti/时钟门控与时基/仿真启动模式），每条含证据链 | ✅ |
| 安全红线 | `program` 需 `-Yes`、`erase` 需 `-Confirm`；cJTAG ccxml 强制；GPIO35-38 复用禁区 | ✅ 实测 program 走 probe→loadti→报告 ccxml/MPN/.out |
| 坑位回写闭环 | 红门禁 → 实证根因 → 修固件/工具 → 落 ADR + targets + docs/40 + 换板必查清单 | ✅ 本轮 2 次（ADR 0005/0006） |
| CI 模板 | `templates/ci/github-actions.yml`（host-tests job） | ⚠️ 仅模板，未实际挂 CI |

---

## 3. 诚实标注的缺口（必须说清，否则不是合格审计）

1. ~~【闭环断点】HIL 腿当前为红~~ **已闭合（2026-10-17）**。板子在位后 `run-hil.ps1 -ComPort COM5` → `HIL RESULT: PASS`（H1-H6），`state.json` hil 段与 `targets/F280039C.json` 同步刷新为 pass。复跑过程本身暴露了两处真实固件缺陷并修好：SCIA 外设时钟未使能→SCI 全静默（ADR 0005）、CPUTIMER 时基被误按 SYSCLK/4 配→流水灯快 4 倍（ADR 0005）。同时修正了 HIL 探针自身的三类方法错误（halt 采样导致 TIF 单标志塌缩把 200ms 测成 426ms；判读误用 GPADAT 锁存而非 GPADAT_R；只读一个字判 32 位寄存器）与工具链三处（`ccs.ps1` 绝对路径 + `SHELL=cmd`、`doctor.ps1` 参数默认值崩溃、`run-hil.ps1` 回写 state.json 未指定 UTF-8 编码）。今天 `records/hil-dss-2026-10-17_*.log` 留下 16 份运行日志（3 份 dss 崩溃无 RESULT 行——正是旧 `fail_reason="no HIL RESULT line (dss crashed?)"` 的真身、9 FAIL、4 PASS），失败码逐一可追溯。**结论：这一腿的价值被验证了——它抓到的不是"接线问题"，而是固件/测试/工具三个不同层面的 bug。**
2. **【纪律 vs 自动化】CI 未实际接入。** 门禁靠 agent/人手动跑，没有在每次变更自动触发 `spec.ps1 check` + `run-host-tests`。当前闭环是「规范驱动」而非「流水线强制」。
3. **【真相源冗余 — 已于 2026-10-18 闭合】** manifest 现已被 `harness/schemas/manifest.schema.json` 校验（`verify.ps1` 的 doctor 腿输出 "manifest validates against manifest.schema.json"）。剩余层面：manifest 仍非 ccxml 的实际消费方（`ccs.ps1` 运行时重跑 `ToolDetect.ps1`），「环境单一真相源」仍是 ToolDetect 运行时发现 + manifest 快照两层——这是有意的缓存设计，非缺陷。
4. **【快照漂移】state 是「信任但需重跑门禁验证」的快照，不是实时真相。** `state.env.checked_at` 与 manifest 生成时间曾有一天时差、`host_tests.at` 落后于重跑日期——本轮已全部刷到 2026-10-17 并与四道门禁输出一一对应。风险仍在：任何人改代码后若不重跑门禁，state 立刻变旧。
5. **【仅一个 spec】** 全流程支持多 spec 与 S/M/L 分级（场景 B），但当前仅 `0001-led-chaser` 一个。场景 B「新功能→spec」路径已 tooling+文档齐备，尚未用第二个真实 spec 演练。
6. **【自动化覆盖不到的三类验收】** REQ-003 的 ±2 % 节拍精度需示波器/逻辑分析仪（HIL 自动门禁只到 ±25 %）；REQ-004 的 fail-safe 快闪需故意改坏 `LED_PATTERN_REF` 后上板注入；「掉电重上电的 Stand-Alone 启动」在探针挂着时不可自动化（RESC.DCON→Emulation Boot，`target.reset()` 永远停在 Boot ROM，见 ADR 0006）。三者已在 `tests/on-target/README.md` 与 `harness/docs/40-烧录与调试手册.md` 的「未覆盖项」诚实清单中登记为人工项，trace 对应 REQ-004 状态为 `pending`（`spec.ps1 check` 因此要求 `spec.json` status=`implementing`，不得提前 done）。
7. **【仓库基线缺失】** `E:/AIcherry` 是 git 顶层仓库，但 harness 全部处于未跟踪状态、**零 commit**。ADR/state/trace 这些"记录系统"目前只存在于工作区文件，没有任何不可变历史兜底；误删或未保存的修改无法回滚。
8. **【工具链缺口 + 环境真相修正】** cmake 未安装，主机测试走 MSVC `cl.exe` 回退路径（docs 记录的设计取舍，非故障）；`clang-cl` 路径未验证。本轮另发现 `doctor.ps1` 用 `Get-Command` 会把微软商店的 `python.exe` 别名桩（exit 9009、什么都不打印）当成可用工具写进 manifest——已改为"跑一次 `--version` 成功才登记"，现 manifest 只记 `py`（真解释器 3.10.4）与 `git`。

---

## 4. 架构师视角映射（H = 六层 + 范式 P）

- **E 执行循环**：`doctor→spec→code→build→host-test→HIL→state`，每次状态跃迁都有 rule 门禁拦截。
- **T 工具注册**：5 个确定性工具 + DSS 脚本，统一经 `ToolDetect.ps1`，无裸访问。
- **C 上下文管理**：manifest/targets/memory/ADR/docs 把上下文外置持久化，降低对模型记忆依赖。
- **S 状态存储**：`state.json` schema 约束快照，pointer+stats 取向正确。
- **L 生命周期钩子**：spec 状态机、DoD 9 项、CI 模板。
- **V 验证接口**：`spec.ps1 check` / `run-host-tests` / `run-hil` 均有显式 PASS/FAIL 出口；2026-10-17 四道（doctor / check / host-tests / HIL）+ 烧录门全部实跑转绿。
- **P 范式**：`rule→agent→rule` 三段式 + kiro-spec + **离线优先**（沙箱无网/无 cmake 驱动的零依赖设计取舍，见 ADR 与 `tests/host/README.md`）。

**逻辑通用性（铁律三）**：工作流规则与工具链无关，数据（目标卡/需求/状态）放在 JSON，未把具体框架写死——符合「逻辑通用、数据可进化」。

---

## 5. 建议的闭环补强（按优先级）

| 优先级 | 动作 | 关闭的缺口 | 状态 |
|---|---|---|---|
| P0 | XDS110 在位后跑通 `run-hil.ps1`→PASS，刷新 `state.json` 的 hil 段 | 缺口 1 | ✅ 2026-10-17 完成（并顺带修好 2 个固件缺陷） |
| P0 | 给仓库建立 git 基线（首次 commit，把 harness 纳入版本控制） | 缺口 7 | ⏳ 待用户授权（AGENTS：commit 只在用户要求时创建） |
| P1 | 把 `spec.ps1 check` + `run-host-tests.ps1` 接进 GitHub Actions（模板已备），每次 PR 强制 | 缺口 2 | ⏳ |
| P2 | 增 `harness/schemas/manifest.schema.json`，doctor 写后自校验 | 缺口 3 | ✅ 2026-10-18 完成（违约 → 记录进 problems 并退出 1） |
| P3 | 用第二个真实 spec（如 UART 桥接）走完场景 B，验证 S/M/L 分级 | 缺口 5 | ⏳ |
| P3 | 人工补测三项仪器级/不可自动化验收（±2 % 节拍、fail-safe 注入、掉电冷启动） | 缺口 6 | ⏳ 需人操作 |
| P3 | 安装 cmake 后走首选 ctest 路径复跑主机测试（当前仅验证 MSVC 回退） | 缺口 8 | ⏳ 可选 |

---

## 6. 最终判定

> **符合。** F280039C-harness 是一套设计成熟、工具齐备、且已实证可运行的 AI coding 闭环生产管理系统，三柱（SPEC/TDD/harness 控制）在机制与证据上均成立。
>
> 2026-10-17 开发板在位后，四条腿（环境 / SPEC / 主机 TDD / 上板 HIL）全部实跑转绿，`probe → program(-Yes) → run-hil` 在真板上贯通。**闭环的价值恰好在最后一腿被证明**：它没有停在"探针掉线"这类环境问题，而是从红门禁倒推出三个不同层面的真实缺陷（固件缺 SCIA 时钟门控、定时器时基配错、HIL 探针自身的 halt 采样方法错误），并按 harness 规矩逐一回写 ADR 0005/0006、`targets/F280039C.json`、docs/40 与换板必查清单。
>
> 剩余的不是架构缺陷，而是三类执行层待办：**流水线强制**（CI 未接入）、**版本基线**（仓库零 commit，记录系统尚无不可变历史）、**仪器级/不可自动化的验收项**（±2 % 节拍、fail-safe 注入、掉电冷启动，已诚实登记为人工项并让 spec 停在 `implementing` 而非虚假 `done`）。补齐 P0(git 基线) 与 P1(CI) 后，即成为一条从需求对齐到目标侧验证全自动贯通的硬闭环。
