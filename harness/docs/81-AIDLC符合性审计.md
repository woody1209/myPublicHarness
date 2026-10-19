# 81 — AIDLC 符合性审计（闭环生产管理可用性）

> 审计视角：Agent Harness 架构师（墨衡，重构轨：审计 → 映射 → 诊断 → 方案）
> 首轮审计：2026-10-18（探针脱连，HIL 未能真实执行）
> **复审：2026-10-18 同日（独立评审 10 条发现 + 用户下达"所有检查都应有门禁"的工作单之后）**
> 审计对象：`F280039C-harness`（200 份非构建产物文件；首审计记录 158）
> 审计依据口径：AI-DLC（IBM/AWS，Kiro 同源，SDD 驱动）为主 + AIDLC 五阶段（A-I-D-L-C）为辅
> 独立评审：**已执行**，返回 10 条发现；本文 §4 逐条给出处置，不再以"自审"结案
> 设计台账：结论 = 三柱机制成立；**首轮把"门禁跑绿"当成"门禁有效"，该判据错误已按 ADR 0009 重判**；
> 本轮把 10 条发现中的 8 条闭合为可执行门禁并做负向自测，2 条（Git 基线、CI 启用）由用户明确延后

---

## 0. AIDLC 术语先对齐（避免各说各话）

本次查证到两套主流口径，本审计**以 AI-DLC（SDD 驱动）为主**，因为用户明确强调「SPEC（SDD）对齐」，且本 harness 自述为「kiro 风格的 spec 模式」（Kiro 即 AWS 的 AI-DLC 工具）。

| 口径 | 阶段 | 关键构件 |
|---|---|---|
| **AI-DLC（IBM/AWS）** 〔来源1〕 | Inception 意图 → Construction 构建 → Operations 运维 | Spec-driven development（SDD）、intent→unit→**bolt**、**steering files**（仓库内 markdown，定义 agent 必须遵守的规则/约束/架构标准/工作流）、NFR 与风险在 Inception 一并捕获 |
| **AIDLC 五阶段（aidlc.io / aidlc.guru）** 〔来源2〕 | **A**nalyze → **I**deate → **D**evelop → **L**aunch → **C**urate | 另有「Ten Rules for AI in the SDLC」十诫作为操作纪律 |

〔来源1〕 IBM Think: https://www.ibm.com/content/adobe-cms/us/en/think/topics/ai-dlc
〔来源2〕 AIDLC Cheatsheet: https://aidlc.io/cheatsheet ｜ AIDLC Framework: http://aidlc.guru/

---

## 1. 三柱 × AIDLC 映射（2026-10-18 真板复跑后的现状）

### 柱一 · SPEC（SDD）对齐 —— ✅ 强对齐

| AIDLC 阶段 | harness 落点 | 实证 |
|---|---|---|
| A Analyze / Inception（意图 + 验收） | `requirements.md`：用户故事 + **EARS 五句式**（REQ-001..005）+ 术语表 + Out of Scope + 未决项 | EARS 句全部可观测可判定，无"尽量/较快" |
| I Ideate / Inception（设计 + 约束） | `design.md` + `targets/F280039C.json`（NFR/约束）+ **ADR 0001-0009** + 三张 draw.io 图 | 重大取舍全部留 ADR；ADR 0009 专管"门禁有效性" |
| Intent → Unit → Bolt | intent=spec 0001；unit=`tasks.md` 的 T-001..T-009；bolt=一次 spec 完整迭代 | T 全勾选，trace 5/5 verified（其中 REQ-003 的 ±2 % 条款带 `waiver_reason`） |
| 批准门 | `draft → requirements-approved → design-approved → implementing → done` | 当前 **done** |
| 机器校验 | `spec.ps1 check`：结构七段 **+ `spec.schema.json`/`trace.schema.json` 实际执行** + `verification` 里每个路径 `Test-Path` + `verified` 却写"延后/未执行"必须给 `waiver_reason` | **CHECK OK**（实测，`verify.ps1` 的 `spec-check` 腿） |

### 柱二 · TDD 保证工程纪律 —— ✅ 强（已含负向验证）

| AIDLC 要求 | harness 落点 | 实证 |
|---|---|---|
| Develop 前先有测试契约 | 纯逻辑进 `source/`（主机可测），硬件隔离 `_hw.c`/`board_*.c` | 20 cases 可离线跑，模块清单唯一所有者 = `harness/tests/host/modules.json` |
| Launch：测试门禁 | `run-host-tests.ps1`（cmake/ctest 或 MSVC 离线回退），判据 `cases > 0 AND failures == 0` | **20 cases / 0 failures**（实测） |
| Launch：行为验证（硬件） | `run-hil.ps1` H1-H6 + SCI 六项 + halt-free 真实节拍测量 | **2026-10-18 10:08 真板 `HIL RESULT: PASS`**（同日 `verify.ps1 -Hil` 连跑四轮，每轮 9 腿全 PASS），H1-H6 全过，H5 mean=**195.0 ms**（四轮 197.0 / 195.8 / 196.5 / 195.0 ms，各 23 样本 / 12 跳变，散布 1.0 %，判据 200 ms ±25 %），PC=0x82C53，串口 `SERIAL-CAPTURE-OK`；记录 `harness/tests/on-target/records/hil-2026-10-18-led-chaser.md` |
| **负向验证**（十诫第 8 条"以更多怀疑对待 AI 产出"） | ① 故障注入：破坏 `LED_PATTERN_REF_FNV1A` → 目标侧 `g_self_test_code=1` + 双灯 fail-safe；② 每条门禁做"应红输入"负向自测 | ① 存证 `harness/tests/on-target/records/fault-injection-2026-10-18-led-chaser.md`；② 见 §5 清单 |
| 门禁必须真的会拦 | ADR 0008 修的是"永远报 PASS"，ADR 0009 把这条升级成判据：**没在坏输入上红过的门禁不算门禁** | 已修；现坏→红、好→绿 |

### 柱三 · harness 软件工程系统控制 —— ✅ 强

| AIDLC 要求 | harness 落点 | 实证 |
|---|---|---|
| **steering files**（AI-DLC 核心构件） | `AGENTS.md`（强制规则/DoD/红线）+ `harness/docs/00-81` + `targets/*.json` | 规则在仓库内、AI 可读、跨会话生效；路线由 `DOC-DEAD-ROUTE` 机器核对（计数见 §5 实测输出，0 死路） |
| 构建设施 | `ccs.ps1` 统一入口（禁裸 cl2000/gmake）+ `doctor.ps1` 环境自发现 + `verify.ps1` 聚合门 | `verify.ps1 -Hil` → **VERIFY OK - 9 legs PASS**；`.text 3881 words` / FLASH 2.58 % / RAM 2.94 % |
| 单一真相源 | manifest(环境) / targets(**只有硬件事实**) / specs(需求) / schemes(契约) / state(**运行时结果唯一**) | 本轮把 HIL 结果从板卡里删掉，只留 `hil_evidence` 指针（见 §4 发现 6） |
| Traceable by default（证据留痕） | HIL records + ADR 0001-0009 + on-target README 运行台账 | 五份 schema 份份有消费方执行（ADR 0009 第 7 条） |

---

## 2. 十诫逐条核对（复审判定）

| # | 十诫 | 判定 |
|---|---|---|
| 1 | 是运营模式变更，不是买工具 | ✅ AGENTS.md + 聚合门已成为日常入口（`verify.ps1`） |
| 2 | 编码变便宜，**验证才是瓶颈** | ✅ 本轮全部工作量都花在验证上：五条负向自测 + 一份聚合门 + 一份判据 ADR |
| 3 | 个体产出 ≠ 组织吞吐 | ➖ 单人固件场景，不适用；产出已固化为仓库资产 |
| 4 | 先治理后扩展 | ✅ ADR + 批准门 + 安全红线**均有规则表兜住**（GPIO35-38 / erase -Confirm 不再只靠文字） |
| 5 | 先有基线再有采用 | ✅ `.text 3881 words` 基线 + `size-delta` 腿（≤1024 words）+ HIL 基线 + state.json |
| 6 | 度量结果而非活动 | ✅ 判定是 PASS/FAIL + 尺寸百分比 + 实测节拍均值，不是"跑了几次"；`SKIPPED`/`NOT RUN` 单独成列 |
| 7 | **人类拥有合并权** | ➖ **有意偏离**：仓库未纳入 Git（`git ls-files` = 0），用户 2026-10-18 决定"git 部分先不严管，后续连上自己的 GitHub 再调整"。复评触发条件＝接入远端时（届时 DoD 绑为 PR 合入门） |
| 8 | AI 产出的标准 = 人类标准，且更怀疑 | ✅ EARS 可判定验收 + 故障注入 + 门禁负向自测三层 |
| 9 | Agent 即服务 | ✅ 工具包装器，禁裸调（`TOOL-BARE-BUILD` 规则机器检查） |
| 10 | 复利资产是**你的上下文** | ✅ ADR 0001-0009 + docs + records + memory；且上下文本身已被门禁约束（死路即红） |

---

## 3. 首轮实跑发现并修复的缺陷（AIDLC 证据完整性）

**现象**：探针脱连（XDS110 `Found 0 devices`）时，`run-hil.ps1` 仍把结果记为
`hil.status = "fail"`，原因写成 `"no HIL RESULT line (dss crashed?)"`。

**危害**：AIDLC 的 Curate 阶段完全依赖证据记录的真实性。把"**板子不在**"记成
"**固件坏了**"，是一次**证据污染** —— 假 FAIL 比诚实的 UNAVAILABLE 更糟，它会让后续
任何人误判为回归。而 `state.schema.json` 的 `hil.status` 枚举**本来就有 `unavailable`**，
脚本却从未使用。

**修复**（`harness/tools/run-hil.ps1`）：
1. 新增 `Invoke-Unavailable`：写 UNAVAILABLE 记录 + `hil.status='unavailable'` + **退出码 3**，
   明确声明"本记录对镜像不持任何证据，不得读作回归"；
2. 运行前**探针存在性预检**（`xdsdfu -e` → Found 0 即短路），不再空等 90 秒串口窗口；
3. 运行中捕获 `Error -260` 同样归类为 UNAVAILABLE。

**验证**：探针缺失时 → 快速退出码 3、`hil.status='unavailable'`、
`fail_reason="XDS110 debug probe not connected (xdsdfu: Found 0 devices)"`，记录首部标注
"NOT a firmware failure"。

**复审重新定位**：这次修复是**必要但不充分**的。它只处理了"证据被写错"，没处理
"证据看起来对但其实什么都没证"。首轮把这条当成唯一的实质缺陷写进结论，正是因为
判据停在"我实跑了、它绿了"。真正的判据在 ADR 0009：**这条门禁能不能红**。

---

## 4. 首轮自审与独立评审的分歧（按正确逻辑收敛）

### 4.1 分歧的根源是判据，不是事实

首轮 §5 写"硬件连接需要你操作（Found 0 devices）"与"不需要为符合 AIDLC 改动任何环境配置"，
同时 §4 又列出 G1/G2 两项 ❌ 缺口——**同一篇文档一边判"不用改"一边判"有两处违反十诫"**，
这个自相矛盾就是分岐的形态。独立评审不问"跑绿了吗"，它问三件事：

1. 这条门禁在坏输入上红过吗？（可证红）
2. 这条门禁的判据是量化且打印出来的吗？（可核对）
3. 同一个事实是不是写在了两个地方？（单一真相）

按这三问重判，首轮的"三柱闭环可用、环境不需要修改"是**判据不足**的结论：
它把机制存在（有 spec.ps1、有 run-hil.ps1）当成了机制有效。本节把评审的 10 条发现
逐条落到可执行门禁或明确延后，并保留首轮已经对的判断（柱一/柱二/柱三的结构映射）。

### 4.2 十条发现的处置

| # | 发现（首轮未识别） | 严重度 | 复审判据 | 处置与落点 | 状态 |
|---|---|---|---|---|---|
| 1 | REQ-003 的 ±2 % 节拍精度记为 `verified`，但从未被任何测量证明 | High | verified 必须指向可判定的实测 | `specs/0001-led-chaser/spec.json` 拆成两条 EARS（±25 % 可裁／±2 % 需仪器）；`trace.json` REQ-003 带 `waiver_reason`；`harness/docs/40-烧录与调试手册.md` 未覆盖项写明 20 ms 采样量子 ≫ 4 ms 带宽；ADR 0009 第 9 条；`spec.ps1` 现在对"verified 却写延后"强制要 `waiver_reason` | ✅ 判据闭合（±2 % 本身仍未证，待仪器） |
| 2 | 仓库无 Git 基线，合并权无处落地 | High | 十诫 #7 | 用户 2026-10-18 明确延后；`harness/docs/50-CI与版本管理.md` 如实标注"未初始化提交"，不再写"已纳管" | ➖ 有意偏离，接入 GitHub 时复评 |
| 3 | GPIO35-38 禁复用只有散文禁止，无机器网 | High | 红线必须可自动违反 | `static-check.ps1`：`PIN-JTAG-3538`（`GPIO_writePin(35..38)` 与 `GPIO_3[5-8]_*` 两种形态）、`PIN-BARE-PINCFG`（driverlib 引脚宏只能出现在 `board_pins.h`）、`PIN-DUPLICATE-TRUTH` | ✅ 闭合（负向自测见 §5） |
| 4 | `spec.ps1` 只查结构，`harness/schemas/*.json` 没有任何脚本读 | Medium | 契约必须被执行，否则删掉契约 | 新增 `harness/tools/lib/JsonSchema.ps1`；`spec.ps1` 执行 spec/trace schema；`verify.ps1` 执行 state schema；`doctor.ps1` 写后自校验 manifest（违约写进 `problems` 并退出 1）；`static-check.ps1` 执行 `project.config.json`（schema + 工程名一致 + 每个 `paths.*` 存在 + `build_config` 有输出目录） | ✅ 闭合（五份 schema 份份有消费方） |
| 5 | 主机测试模块清单在三处重复（CMakeLists / test_runner / 文档） | Medium | 同一事实只写一处 | 唯一所有者 `harness/tests/host/modules.json`；`TEST-MODULE-UNDECLARED`／`TEST-CMAKE-DRIFT`／`TEST-CMAKE-STALE` 双向核对 | ✅ 闭合 |
| 6 | HIL 结果两份真相（板卡 `pass` vs state.json `unavailable`） | Medium | 运行时状态唯一真相 | `targets/F280039C.json` 的 `hil` 段删除，改为 `hil_evidence` 指针（指 state.json + records 目录）；`harness/docs/40-烧录与调试手册.md` 表不再携带状态；`harness/docs/81-AIDLC符合性审计.md`/`README` 只引用记录文件 | ✅ 闭合 |
| 7 | size 报告把 C28x 的**字**当**字节**，flash 占用减半 | Medium | 单位与预算来自硬件卡 | `ccs.ps1 size` 表头打印"16-bit WORDS（1 word = 2 bytes）"，预算读 `targets/F280039C.json`（196608／35328 words）；1.55 % 纠正为 **2.58 %**；`size-report.json` 机读 + `size-delta` 腿比对 state 基线 | ✅ 闭合（ADR 0002 一致性恢复） |
| 8 | AGENTS.md 四条强制路径指向不存在的文件 | Medium | 地图必须可走 | 四条全部改到真实文件；`.workbuddy/**` 越界要求删除（与 §6 冲突）；新增 `DOC-DEAD-ROUTE` 扫 AGENTS/README/docs/specs/tests/env 的路线令牌 | ✅ 闭合（193 条路线，0 死路） |
| 9 | DoD 里 program 排在 probe 之前，顺序反了 | Medium | 前置条件必须是硬前置 | `AGENTS.md` §2：#7 连接确认标注为 #8 的**硬前置**；§3 判据改为 `xdsdfu -e` 输出 `Found N device`（N ≥ 1）并把设备名/序列号报告给用户 | ✅ 闭合（本轮实跑即按 probe→build→program→verify 执行） |
| 10 | CI 模板以 `cppcheck … \|\| true` 结尾，文档却写"默认开启" | Medium | 软失败门禁 = 没有门禁 | `harness/templates/ci/github-actions.yml` 头部改「TEMPLATE - NOT ENABLED」，cppcheck 用 `--error-exitcode=1` 且删除 `|| true`，artifact 步骤加 `if: always()`；`harness/docs/50-CI与版本管理.md` 明确"模板，尚未启用"并说明 HIL 留本机是合理偏离 | ➖ 缺陷闭合，**启用本身**由用户延后 |

### 4.3 复审中我自己新查出的两处

| # | 发现 | 处置 |
|---|---|---|
| 11 | `run-hil.ps1` 的 `Invoke-Unavailable` 分支用 `Get-Content -Raw`（无 `-Encoding`）读 `state.json` 再序列化——PS 5.1 会把无 BOM 的 UTF-8 按 GBK 解，一旦触发就会把带中文的运行真相源写成乱码并落盘 | 三处调用点补 `-Encoding UTF8`（含 `ccs.ps1 help`、`test-loadti.ps1`）；新增 `PS-READ-ENCODING` 规则，任何 `-Raw` 读不带 `-Encoding` 即红（已用 scratch fixture 负向自测到 exit 1） |
| 12 | `AGENTS.md` §2 声称 `verify.ps1` "跑完下表全部软件门禁"，但 #7/#8/#12 并不在聚合门的腿里——这句本身就是首轮那类"看起来覆盖了"的措辞 | §2 现在逐条列出腿映射（#0-#6、#9、#10 在门内，#11 经 #0 的证据路径核对覆盖，#7/#8/#12 明写在门外的执行方式） |

---

## 5. 门禁有效性判据落地（本轮实际改动与负向自测）

判据：**没在坏输入上红过的门禁不算门禁**（ADR 0009 第 1-2 条）。

聚合门（真板，最后一次 2026-10-18 10:08；同日上午连跑四轮，每轮 9 腿全 PASS）：

```
.\harness\tools\verify.ps1 -Hil
→ VERIFY OK - 9 legs PASS（退出码 0）
```

| 腿 | 可执行判据（由门禁自己打印） | 实测 |
|---|---|---|
| doctor | `environment OK` + manifest 过 `manifest.schema.json` | PASS（探针 SN=CL391078 已枚举） |
| static-check | `STATIC CHECK OK (0 violations)`：16 firmware / 12 ps1 / 3 manifest modules / 193 doc routes + `cl /analyze` | PASS |
| host-tests | `cases > 0 AND failures == 0` | 20 / 0 |
| spec-check | `CHECK OK` + schema + 证据路径 + waiver 强制 | PASS |
| state-schema | `state.json` 过 schema，0 violations | PASS |
| build | 目标 CONFIG 链接退出 0 | PASS |
| size | FLASH ≤ 90 % AND RAM ≤ 90 %（**字**） | 2.58 % / 2.94 % |
| size-delta | `\|.text − 基线\| ≤ 1024 words` | 3881 vs 3881，delta 0 |
| hil | `HIL RESULT: PASS` 且 H1-H6 每条 PASS；H5 200 ms ±25 % 且 ≥8 跳变 | PASS，mean 195.0 ms / 12 跳变（四轮 195.0–197.0） |

负向自测（新门禁上线必须做，构造应红输入 → 确认退出码非 0 → 清理样本）：

| 样本 | 预期 | 实际 |
|---|---|---|
| `verify.ps1 -MaxFlashPct 2` | 体积腿红 | `size gate FAIL` → `VERIFY FAIL`，退出 1 |
| `verify.ps1 -MaxTextDeltaWords -1` | 增量腿红 | `size-delta FAIL`（delta 0 > -1） |
| `verify.ps1 -Skip host-test`（腿名打错） | 未知跳过名不能静默放行 | `USAGE: unknown leg` → 退出 **2** |
| 违约 `manifest.json` 样本 | doctor 写后自校验必须抓出 | 4 处 schema 违约全部报出并计入 `problems`，退出 1 |
| 违约 `project.config.json` | config 腿必须执行契约 | `build_config=CPU2_FLASH`／`fpu=fpu64` 两处 enum 违约 + `paths.host_tests` 指向不存在目录 → `CFG-PATH` 红（样本已还原并复跑绿） |
| `Get-Content -Raw` 无 `-Encoding` 的 fixture | 新规则必须能红 | `PS-READ-ENCODING` 命中，退出 1（fixture 已删） |
| 故障注入（REQ-004 负向） | 坏常量必须被上板自检抓到 | `g_self_test_code=1` + 双灯 fail-safe，存证 records |

> HIL 记录按**日期**命名，每次真板运行都会覆盖当日那一份 md（原始 dss/sci 日志按时间戳分别留档）。
> 因此节拍数字的权威来源是记录文件本身，散落在文档里的数字只是引用；跨日/跨轮散布见 §4.2 发现 1。

跳过 ≠ 通过：`-Skip` 或省略 `-Hil` 时矩阵打印 `VERIFY INCOMPLETE` 并退出 1，
`hil` 腿显示 `NOT RUN` 且判词明写"本结论不声称该项"。

---

## 6. 遗留（用户决定 + 客观限制，均已在仓库内如实标注）

| 编号 | 缺口 | 违反的 AIDLC 要求 | 现在写在哪里 | 复评触发条件 |
|---|---|---|---|---|
| G1 | 仓库未纳入 Git，无 PR/合入门 | 十诫 #7；AI-DLC「Traceable by default：markdown **and Git**」 | `docs/50-CI与版本管理.md`（"未初始化提交"） | 用户接入 GitHub 时（已明确"先不严管"） |
| G2 | CI 模板未启用（仓库根没有 `.github` workflow 目录） | AI-DLC Deploy「测试通过才允许晋升」 | 模板头部 + `harness/docs/50-CI与版本管理.md`「模板，尚未启用」 | 同上；届时把 `verify.ps1` 软件腿接 Actions，HIL 保留本机 |
| G3 | ±2 % 节拍精度无证据 | 验收条款必须可判定 | `trace.json` REQ-003 `waiver_reason` + `harness/docs/40-烧录与调试手册.md` 未覆盖项 | 示波器／逻辑分析仪接入（用户明确"外部设备先不执行"） |
| G4 | 断电冷启动（Stand-Alone Boot）只能人工 | Launch 阶段行为验证 | `harness/docs/40-烧录与调试手册.md` 未覆盖项 + `tests/on-target/README.md` | 人工执行一次并补记录 |
| G5 | cppcheck／clang-tidy 本机未安装，静态分析走 `cl /analyze` | 静态检查覆盖面 | `static-check.ps1` 头注 + `harness/docs/50-CI与版本管理.md` 作业清单 | 装上后加进 `static-check.ps1`，而不是只改 CI 模板 |

---

## 7. 结论：整套环境是否需要修改？

**分三层回答（本轮按"环境本身"与"门禁有效性"分开判）：**

| 层 | 是否需要修改 | 说明 |
|---|---|---|
| **工具链 / SDK / 构建 / 规格 / 单测** | ❌ 不需要 | 本轮实测全绿且可证红：doctor OK、`spec check` CHECK OK、`.text 3881 words`/`.args 512`/`__c_args__` 已定义、主机单测 20/0、size 2.58 %/2.94 %、spec 0001 = done |
| **硬件连接** | ✅ 已具备，并已真实执行 | 探针在位（`Found 1 device`，XDS110 SN=CL391078 FW=3.0.0.43）→ `build` → `program -Yes`（program OK，退出 0）→ `verify.ps1 -Hil` = **VERIFY OK - 9 legs PASS**，HIL 侧 H1-H6 全过、mean 197.0 ms、串口 12 行心跳。**首轮"HIL 未执行"的缺口已闭合** |
| **harness 工具本身** | ✅ 本轮改了，而且这是主要工作量 | 独立评审 10 条发现里 8 条闭合为可执行门禁（§4.2），另做 7 项负向自测（§5），新增聚合门 `harness/tools/verify.ps1` + `harness/tools/lib/JsonSchema.ps1` + `harness/schemas/manifest.schema.json`，规则表加了 `DOC-DEAD-ROUTE`／`PS-READ-ENCODING`／`PIN-BARE-PINCFG`／`PIN-JTAG-3538` 第二形态与 project.config 契约腿（6 个 `CFG-*` 判据），并把两处真相重复（板卡 HIL、`harness/docs/40-烧录与调试手册.md` 状态列）收敛为单点 |

**一句话**：三柱闭环在机制与证据上成立，但首轮"环境不需要修改"的结论**判据不足**——它没问"门禁能不能红"。
按 ADR 0009 重判后，本轮真正需要修改的是 harness 自己：**10 条发现里 8 条已变成会红的门禁，2 条由用户明确延后**；
环境（工具链／SDK／板子）经全链实跑确认无需改动，HIL 已在真板执行并通过。

**当前唯一"未闭合但已如实标注"的项**：G1/G2（Git + CI 基建，用户延后）、G3（±2 %，待外部仪器）、
G4（断电冷启动，人工）、G5（cppcheck 未装）。它们都以 `waiver_reason`／未覆盖项／模板头
的形式写在仓库里，**不再以"已达成"出现**。
