# 00 · Harness 工作流总览（AI 固件生产流水线）

> **本文是 harness 的入口文档**，回答两个问题：
> 1. **换一块板子 / 换一颗 MCU**，AI coding 工作怎么开展？
> 2. **加一个新功能 / 改一个既有行为**，怎么在 harness 的各设施里流转落地？
>
> 规则的强制版在 `AGENTS.md`；本文给的是**全流程地图 + 每步落在哪个设施**。
> 本仓库（f280039c-harness）本身就是"换平台"这条流程的产物——文中以它自证。

---

## 1. 一条主线：rule → agent → rule

```
[rule]   确定性判断用脚本：doctor 探测环境、ccs 构建/烧录、host-tests 回归、spec check 校验
[agent]  生成性内容交 Agent：spec 三件套正文、固件 C 代码、ADR 决策说明
[rule]   脚本负责收口：check 通过 = 规格侧完成；DoD 清单打满 = 交付侧完成
```

原则：**确定性的东西不交给 LLM 猜，生成性的东西不写死脚本。**
任何"AI 说做完了"都不算数，必须有一条可执行命令作证（构建输出 / 测试退出码 / 上板证据）。

---

## 2. 设施地图

| 设施 | 角色 | 契约 / 真相源 |
|---|---|---|
| `harness/tools/doctor.ps1` | 环境自发现，生成 `harness/env/manifest.json` | manifest 未生成/不符 → **先修 doctor 再干活** |
| `harness/tools/ccs.ps1` | 构建 / 尺寸 / 烧录唯一入口（env/build/rebuild/clean/size/probe/program/erase/monitor） | 禁止裸调 `cl2000`/`gmake` |
| `harness/tools/run-host-tests.ps1` | 主机侧纯逻辑单元测试回归 | 退出码 0 |
| `harness/tools/run-hil.ps1` | 硬件在环（DSS H1–H6 + SCI 串口捕获 + 证据留档） | `harness/tests/on-target/` |
| `harness/tools/spec.ps1` | spec 新建/列表/校验/追溯（new/list/check/trace） | `harness/schemas/spec.schema.json` + `harness/schemas/trace.schema.json` |
| `harness/tools/static-check.ps1` | 代码/脚本/文档路线的静态门禁（含 DOC-DEAD-ROUTE） | `STATIC CHECK OK (0 violations)` |
| `harness/tools/verify.ps1` | 一条命令串起聚合门的全部腿（9 条；#7 连接确认、#8 loadti 烧录、#12 文档回写不在其中，见 AGENTS.md §2 的覆盖范围说明），退出码即 DoD 结论 | 跳过 = `VERIFY INCOMPLETE` + 退出 1 |
| `specs/<id>/` | 需求→设计→任务→追溯的规格档案 | 批准门状态机驱动 |
| `targets/<MPN>.json` | 芯片规格卡：**板级事实的单一真相** | LED/串口/调试/字地址/启动顺序/loadti 段 |
| `projects/<proj>/` | 参考工程（spec 的活范例 + 新坑的踩平场） | `project.config.json` + `state.json` |
| `harness/docs/` | 环境总览/生产规范/编码规范/烧录调试/CI + `adr/` | 踩坑与决策的沉淀地 |
| `harness/docs/10-环境总览.md`「环境修复备忘（踩坑记录）」 | 环境类发现的落盘处 | 坑只踩一次 |

---

## 3. 场景 A：更换板子 / 更换 MCU（Onboarding 八步）

> 实证依据：本仓库即从 `C3M3-harness`（Infineon PSC3M3，**只读参考，严禁修改**）
> 同构移植而来，且经历过一次"同家族换板事实"修正（C2000Ware 默认 31/34 是 controlCARD
> 引脚，LaunchPad 实为 GPIO20/22）。

### A1. 决策：harness 内扩展 vs 新建 harness

| 情况 | 做法 |
|---|---|
| 同工具链/SDK 家族内换板或换型号（如 F28003x 内 LaunchPad ↔ controlCARD） | 在**现有 harness 内**新增 `targets/<MPN>.json` + 新参考工程 |
| 换编译器/SDK 体系（如 C2000 → PSOC） | **新建 harness 仓库**，克隆参考 harness 的骨架结构（schemas/tools/specs 模式/工作流），目标相关内容全部重填 |

无论哪条路，**结构同构、内容重填**：契约（schemas）、三段式模型、spec 模式直接继承；
环境路径、目标卡、踩坑记录必须从零实证，禁止沿用参考仓库的目标相关结论。

### A2. 环境自发现（第一步，不可跳过）

```powershell
.\harness\tools\doctor.ps1        # 生成/校验 harness/env/manifest.json
```

- manifest 记录 CCS 根目录、编译器版本、SDK（C2000Ware）路径——**一切构建/烧录命令的
  路径真相源**。manifest 缺失或与本机不符时，先修 doctor，禁止凭记忆猜路径
  （本机教训：C 盘无 `C:\ti`，TI 工具链全在 `D:\ti\ccs2101`）。
- 工具链/SDK 升级 = L 级变更：重跑 doctor + 更新 manifest + ADR/提交信息说明。

### A3. 建立目标卡 `targets/<MPN>.json`

按实证进度分两阶段填，**没实证的字段不许编**：

1. 数据手册/产品页可查的：core/封装/Flash/SRAM 布局/时钟能力/外设清单/安全特性。
2. 必须上板实证后回填的（本仓库已填的段，照此结构抄）：
   - `gpio_registers_word_addresses`：字地址寄存器表 + LED 所在位；
   - `address_model`：字地址模型事实 + 证据链（引 ADR）；
   - `startup_order`：启动顺序 + ramfunc 事实（引 ADR）；
   - `loadti`：`--args=` 链接约束与自检法（引 ADR）；
   - `board`：板名、用户 LED（引脚 + 极性）、调试探头（cJTAG 2-pin）、虚拟 COM 引脚。
   每条板级事实注明来源（如 TI SPRUJ31 §3.1.3）。

### A4. 换板必查清单（硬件事实，逐条实证后才许写进代码）

| # | 必查项 | 违反后果 / 本仓库实证 |
|---|---|---|
| 1 | **调试接口配置**：LaunchPad 类板载 XDS110 用 cJTAG (1149.7) 2-pin（ccxml `SWD Mode Settings=4`）；直接取 C2000Ware 自带 `<MPN>_LaunchPad.ccxml` | 用标准 4 线 JTAG 报 `Error -2131` 连不上目标 |
| 2 | **用户 LED 引脚以板厂用户指南为准，不是 SDK 示例** | C2000Ware 默认 31/34 是 controlCARD；LaunchPad 实为 GPIO20/22，抄错=灯不亮还误判固件 |
| 3 | **LED 极性**（低电平点亮经 SN74LVC2G07 类驱动） | 极性反="该亮的不亮" |
| 4 | **C28x 字地址模型**：`.cmd` origin、map 地址与 size、反汇编、DSS `readWord`、`PC`/`XARn` 全是 16 位字；32 位寄存器读两个字拼接 | 曾据此误判"driverlib 有 2× bug"——五方实证 driverlib 无罪（ADR 0002），**禁止绕开 driverlib** |
| 5 | **`.TI.ramfunc` 启动顺序**：`main()` 首动作 = ramfunc 重定位 + ready 校验，然后禁看门狗 → PIE → 时钟 → 外设 | 违反=启动几毫秒即死，无日志无灯（ADR 0003） |
| 6 | **loadti 链接约束**：Makefile LDFLAGS 必须 `--args=<n>` 定义 `__c_args__` | 缺了 `ccs.ps1 program` 报 4011；HIL 路径不报，属"静默 half-broken"（ADR 0004） |
| 7 | **上拉默认关闭**：GPxPUD 复位全 1，未配置引脚悬空拾噪声 | 判读只认目标引脚所在字（LED 看 GPADAT 高字或 GPADAT_R） |
| 8 | **JTAG 引脚红线**：GPIO35–38 禁复用为 GPIO | JTAG 永久失效，板子变砖（调试层面） |
| 9 | **外设时钟门控**：F28003x 复位后 CPUSYS `PCLKCRn` 全关，用哪个外设先 `SysCtl_enablePeripheral()`（SCIA=PCLKCR7@0x5D330 bit0） | 门控未开时对改外设帧**每次写都被静默丢弃**、无任何错误码：`CTL1` 恒 0、串口 0 字节（ADR 0005；HIL `H3-sciclk` 已钉住） |
| 10 | **CPUTIMER 时基 = SYSCLK**（TPR=0），且 **TIF 是单标志**：时序只能在核心运行时 peek 测量 | 按 SYSCLK/4 算周期 → 节拍快 4 倍；"halt—读—run"采样把 200ms 报成 426ms（2026-10-17 两次误判，ADR 0005） |
| 11 | **仿真器在线即 Emulation Boot**：F28003x 无 `-TRST`，boot ROM 看 `RESC.DCON`(0x5D380 bit31)；仿真引导键无效 → 落 Wait/SCI 引导 | `target.reset()` 后 PC 恒停 0x3FB8B9、灯不动，会被误判成"烧录失败/固件不跑"。H6 用 `PC=code_start` 等价冷启动；断电重启路径无法用探针自动验证（ADR 0006） |

### A5. 移植参考工程（新平台第一件固件）

参考工程是 harness 的**活契约**：所有平台坑先在这里踩平、修复、落盘，业务工程直接受益。

```powershell
.\harness\tools\spec.ps1 new -Id 0001-<reference-feature> -Title "<参考功能>"
```

移植清单（从既有参考工程拷 `Makefile` / `device/` / `source/` 骨架时逐项核对）：

- **Makefile 四处保留**：`--args=0x200`（loadti，见 A4-6）、`.out` 目标依赖含 `Makefile`
  本身（否则改 LDFLAGS 不触发重链，stale 镜像直接出货——本仓库真实踩过）、
  **`.obj` 前置依赖含 `HEADERS`**（cl2000 的 `.d` 用反斜杠路径，gmake 的 `-include`
  识别不了，改头文件不触发重编——见 **ADR 0007**）、
  Windows cmd.exe 兼容（无 `mkdir -p`/`rm -rf`）。
- **分层结构**：`source/<module>.c`（纯逻辑，主机可测）+ `source/<module>_hw.c`
  （driverlib/寄存器，只进 target 构建）+ `source/board_*.c`（时钟/看门狗/引脚）。
- **`source/board_pins.h`**：引脚符号唯一定义处；业务代码只许用符号名，禁止裸引脚号。
- **启动顺序样板**：按 A4-5 的 5 步顺序写 `main()`，ramfunc 重定位 + ready 校验做第一个动作。
- **可观测性**：`sci_log`（SCIA → XDS110 虚拟 COM，115200-8N1，TX/RX 引脚以目标板为准）。
- **编码陷阱**：C28x 无 8 位类型（`uint8_t` 不存在，`CHAR_BIT=16`）；driverlib
  `--define=DEBUG` 需应用提供 `__error__`（`board_error.c`）。

### A6. 参考工程验证闭环（聚合门 9 腿全过才算 onboard 完成）

```
verify.ps1 -Hil ──▶ VERIFY OK（一条命令跑完下列 9 腿；退出码即 DoD 结论）
   doctor · static-check · host-tests · spec-check · state-schema
   build · size · size-delta · hil
```

| 腿 | 单腿命令 | 覆盖 |
|---|---|---|
| doctor | `doctor.ps1` | 环境自发现 + manifest 过 schema |
| static-check | `static-check.ps1` | 规则扫描 + `cl /analyze /W4`，0 违规 |
| host-tests | `run-host-tests.ps1` | 纯逻辑回归（20 cases / 0 failures） |
| spec-check | `spec.ps1 check` | 规格结构 + trace 全覆盖 + 证据路径存在 |
| state-schema | （内建） | `state.json` 过 `state.schema.json` |
| build / size | `ccs.ps1 build\|size` | 双配置可编 + FLASH/RAM 预算（C28x **字**） |
| size-delta | （内建） | `.text` 与 state 基线差 ≤ 1024 字 |
| hil | `run-hil.ps1` | 上板 DSS H1-H6 + SCI 串口断言；退出码 3 = 探针不在 = UNAVAILABLE |

- 每腿对应 spec 里的 EARS 可执行验收（命令或断言）；
- 双配置：能编 `CPU1_RAM` 的一并编（L 级变更的回归要求）；
- **跳过某腿 = 该判据未被证明，按失败处理**（ADR 0009：`VERIFY INCOMPLETE` 退出 1）；
- HIL 判读规则：读外设前必须 halt；`g_self_test_code` 单独判读无效（静态初值为 0），
  必须与 H5 节拍 + SCI banner 组合；证据按
  `harness/tests/on-target/README.md` 清单留档 `records/`；
- 烧录路径（loadti）不在 9 腿内，单独跑 `ccs.ps1 program -Yes` 留证（见 AGENTS.md §2 覆盖范围）。

### A7. 知识落盘（换板产出物清单）

1. `targets/<MPN>.json` 实证段回填（A3 第 2 阶段）；
2. `harness/docs/10-环境总览.md`「环境修复备忘（踩坑记录）」追加（每条带"现象→根因→对策"）；
3. 重大取舍写 `harness/docs/adr/NNNN-*.md`（照 `template.md`）；
4. 参考工程 README（行为约定表 + 基线体积表）+ 根 README（已验证环境表）；
5. `projects/<proj>/state.json` 快照更新；
6. 全链路复现一次：`.\harness\tools\verify.ps1 -Hil` 输出 `VERIFY OK`（换板后的平台就绪证据）。

### A8. 换板 DoD（全部打勾才算平台就绪）

- [ ] doctor manifest 与本机一致，路径无猜测
- [ ] 目标卡板级字段全部有来源标注（数据手册 / 用户指南 / 实证）
- [ ] A4 必查清单逐项有结论（文档或 ADR）
- [ ] 参考工程 spec `check` 通过
- [ ] `verify.ps1 -Hil` 输出 `VERIFY OK`（9 腿）+ `ccs.ps1 program -Yes` 留证
- [ ] 踩坑全部落盘 `harness/docs/10-环境总览.md`；重大取舍有 ADR
- [ ] `.text` 基线与增量来源记录在 state.json / README

---

## 4. 场景 B：新功能变化（六步）

### B1. 先判定：要不要 spec，哪个级别

**判定口径：改动会引入新的对外行为或新的硬件依赖 → 必须 spec。**
拿不准按"必须 spec"处理。

| 级别 | 例子 | 要求 |
|---|---|---|
| S | `source/*.c` 纯逻辑改动 | build + host tests + README/docs；新模块/新行为需先 spec |
| M | 时钟策略 / 引脚分配 / 链接脚本 / 启动顺序 / 外设初始化 | spec + regen-equivalent（手工同步 `board_pins.h` 与 target card）+ ADR/docs |
| L | 工具链/SDK 升级、RTOS 引入 | spec + ADR + 双配置构建 + 回归 |

免 spec 仅限：改注释、改日志文案、单文件小修（仍走 DoD）。

### B2. spec 三件套 + 批准门（硬规则）

```powershell
.\harness\tools\spec.ps1 new -Id 0002-<slug> -Title "<特性名>"
```

```
requirements.md ─[用户确认]▶ design.md ─[用户确认]▶ tasks.md ─▶ 实现 ─▶ 勾选+trace ─▶ spec.ps1 check
```

- `spec.json` 的 `meta.status` 状态机：
  `draft → requirements-approved → design-approved → implementing → done`（受阻 `blocked`）。
- **未获用户确认，不得越过 requirements/design 直接写代码**；实现中需求要变 →
  先改 spec 重新确认，再改代码（spec 与代码双向一致）。
- 每条 REQ：唯一编号不跳号 + 一条 EARS 验收句（可观测、可判定，禁止"尽量快"）+
  一条可执行验收（命令/断言）+ 至少一个 task 覆盖。
- `trace.json` **恰好覆盖**全部 REQ：文件 / 测试 / 验证命令 / 状态；
  `status=done` 要求任务全勾选且 trace 全 `verified`（或 `waived`）。
- `spec.ps1 check` 退出码 0 = 规格侧完成。

EARS 句式速查：普遍 `THE SYSTEM SHALL <行为>`；事件 `WHEN <触发>，THE SYSTEM SHALL…`；
状态 `WHILE <状态>，…`；异常 `IF <条件>，THEN…`；可选 `WHERE <特性>，…`。

### B3. 代码落点（分层铁律）

```
source/<module>.c     纯逻辑：只依赖 C 标准库 + 本仓库头文件 → 主机侧可单测
source/<module>_hw.c  硬件相关：driverlib / 寄存器 → 只进 target 构建
source/board_*.c      系统级：时钟 / 看门狗 / 引脚分配
device/               器件相关源（随器件）
```

- 新引脚只进 `targets/<MPN>.json` 与 `source/board_pins.h` 各一次，业务代码用符号名；
- 禁止 `sprintf` 到定长缓冲：格式化走 `fmt`（带长度边界、可主机单测），发送走 `sci_log`；
- 启动顺序/链接红线见 A4-5/A4-6，新工程改动不得破坏。

### B4. 单一真相同步（M 级必做）

| 变化 | 同步点 |
|---|---|
| 引脚分配 | `targets/<MPN>.json`（`board`/`gpio` 段）+ `source/board_pins.h` |
| 时钟策略 | `source/board_clock.c` + target 卡 `clock` 段 + ADR |
| 新外设 | target 卡 `peripherals` 实证注记 + docs/40 操作说明 |
| 行为变化 | 工程 README 行为约定表（对外可见行为的索引） |

### B5. 验证闭环（聚合门 + 判读规则）

```powershell
.\harness\tools\verify.ps1 -Hil      # 一条命令 9 腿：doctor/static-check/host-tests/
                                     # spec-check/state-schema/build/size/size-delta/hil
.\harness\tools\ccs.ps1 program -Project projects\<proj> -Yes   # loadti 烧录路径（不在 9 腿内）
```

- 退出码即 DoD 结论：`VERIFY OK` / `VERIFY INCOMPLETE` / `VERIFY FAIL`；
  **跳过某腿 = 该判据未被证明，按失败处理**（ADR 0009）；
- spec 的 EARS 验收句逐条对应到腿上的命令或上板观察点；
- HIL 判读三坑（2026-10-17 实证）：读外设前 halt；32 位寄存器读全两字；
  H3 自检码与 H5 节拍 + banner 组合判读；
- 破坏性操作红线：`erase` 必须 `-Confirm` 且用户明确确认；未 `probe` 确认不烧录；
  一次只烧一个工程；烧录前后报告 ccxml / MPN / `.out` 路径与结果。

### B6. 落盘与留痕（完成的定义）

1. `specs/<id>/` 任务勾选 + `trace.json` 填实 + `check` 通过；
2. `projects/<proj>/README.md` 行为约定表更新（新行为一行）；
3. 需要时 `harness/docs/adr/NNNN-*.md`（新坑/新取舍）与
   `harness/docs/10-环境总览.md`「环境修复备忘（踩坑记录）」追加；
4. `projects/<proj>/state.json` 快照（`.text` 等统计；增量 >2KB 说明来源）；
5. 提交信息含：改了哪些工程、构建结果、测试结果；
6. 上板证据留档 `harness/tests/on-target/records/`（run-hil.ps1 自动写）。

---

## 5. 两场景叠加：新板 + 新功能

顺序固定：**先走场景 A 把平台与参考工程做实（A1–A8），再在参考工程之上走场景 B 做功能。**
禁止在平台事实未实证的板上直接堆功能——那等于把平台坑混进功能 bug，排障成本翻倍。

---

## 6. 每次会话的标准开场（任何场景都适用）

```powershell
# 1. 环境自检 + 读 manifest
.\harness\tools\doctor.ps1
# 2. 读规则：AGENTS.md（强制版）→ 本文（流程地图）→ 对应 docs（细节）
# 3. 查状态：.\harness\tools\spec.ps1 list ；看 projects\<proj>\state.json
# 4. 动手：按场景 A 或 B 执行；每完成一步更新对应设施
# 5. 收口：verify.ps1（行为改动加 -Hil）输出 VERIFY OK + 知识落盘
```

---

## 7. 相关文档

| 想了解 | 去 |
|---|---|
| 强制规则（必读） | `AGENTS.md` |
| 环境/工具链/踩坑 | `10-环境总览.md` |
| 为什么这样管 AI 生产 | `20-AI生产规范.md` |
| 编码与单测细则 | `30-编码与单测规范.md` |
| 烧录/调试/故障表/启动顺序 | `40-烧录与调试手册.md` |
| CI 与版本管理 | `50-CI与版本管理.md` |
| spec 模式详解 | `70-Spec驱动开发.md` / `specs/README.md` |
| 决策记录（0001–0008） | `harness/docs/adr/` |
| 三柱闭环符合性审计 | `harness/docs/80-harness闭环符合性审计.md` |
| AIDLC 符合性审计 | `harness/docs/81-AIDLC符合性审计.md` |
| **图示**（环境配置总览 / 场景A 换板 / 场景B 新功能） | `harness/docs/diagrams/*.drawio`（draw.io 可编辑，布局经 `check_layout.py` 校验无重叠） |
