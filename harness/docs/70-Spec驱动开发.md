# 70 · Spec 驱动开发（kiro 风格）

本仓库沿用 `C3M3-harness` 的 spec 模式（结构与目录约定保持一致）：为什么需要、怎么用、
和既有设施怎么衔接。操作入口是 `specs/README.md`（规则速查）与 `harness/tools/spec.ps1`。

---

## 1. 为什么需要 spec

本环境的强项是**环境与执行**：`doctor.ps1` 让环境可自描述，`ccs.ps1` 让构建可复现，
`run-host-tests.ps1` 让逻辑可回归。短板仍在"前端"：一个需求从哪来、为什么这么设计、
做没做完、有没有被验证——这些散落在记忆与提交信息里，不可检索、不可校验。

spec 模式把这条链路补成**结构化、可校验、可追溯**：
需求（REQ）→ 设计（落点）→ 任务（T）→ 代码/测试/验证命令，一环扣一环。

---

## 2. 什么时候必须写 spec

**改动会引入新的对外行为或新的硬件依赖 → 必须 spec。** 详见 `specs/README.md` §1 判定表。
免 spec 的小改动仍走 `AGENTS.md` §2 的 DoD。

---

## 3. 三件套与批准门

```
specs/<NNNN-slug>/
├── requirements.md   ① 用户故事 + EARS 验收（REQ-XXX）+ 可执行验收
├── design.md         ② 架构/接口/时序/内存/风险 + 需求覆盖表
├── tasks.md          ③ 可勾选任务（T-XXX，标注关联 REQ 与验证命令）
├── spec.json         机读镜像（工具消费）
└── trace.json        追溯矩阵（REQ → 文件 / 测试 / 验证命令 / 状态）
```

**批准门（硬规则）**：

```
① requirements.md ──[用户确认]──▶ ② design.md ──[用户确认]──▶ ③ tasks.md ──▶ 实现 ──▶ 勾选 + trace ──▶ check
```

- 未获用户确认，**不得**越过 requirements/design 直接写代码。
- 确认动作 = 把 `spec.json` 的 `meta.status` 从 `draft` 改为 `requirements-approved`
  （再改 `design-approved`），并同步 md 顶部状态行。
- 实现中若发现需求要变：**先改 spec 并重新确认，再改代码**。

状态机：

```
draft ─▶ requirements-approved ─▶ design-approved ─▶ implementing ─▶ done
                                                        └─▶ blocked（受阻，注明原因）
```

---

## 4. 三段式执行模型

```
[rule]  确定性规则用脚本        spec.ps1 check 校验结构/引用/覆盖/勾选一致性
[agent] 内容由 Agent（或人）写   requirements/design/tasks 正文与固件代码
[rule]  脚本负责收口            spec.ps1 check 通过 = 规格侧完成
```

原则：**确定性的东西不交给 LLM 猜，生成性的东西不写死脚本。**

---

## 5. 工具用法

```powershell
# 新建（从 specs/_template 复制并填占位）
.\harness\tools\spec.ps1 new -Id 0002-uart-bridge -Title "UART 桥接"

# 列出全部 spec 与状态
.\harness\tools\spec.ps1 list

# 校验（退出码 0=通过 / 1=失败 / 2=用法错误）
.\harness\tools\spec.ps1 check
.\harness\tools\spec.ps1 check -Id 0001-led-chaser

# 打印追溯矩阵
.\harness\tools\spec.ps1 trace -Id 0001-led-chaser
```

`check` 校验项：

1. 五个文件齐备；
2. `spec.json` 可解析、`meta` 必填齐全、`spec_id` 与目录名一致；
3. REQ 编号格式（`REQ-\d{3}`）、唯一、**顺序不跳号**；
4. 每条 REQ 具备 `title/user_story/priority/category/ears/acceptance/tasks`，
   枚举合法，EARS 句含 `THE SYSTEM SHALL`；
5. `REQ.tasks` 引用的任务存在于 `tasks.md`；每条 REQ 被 `tasks.md` 至少一个任务行引用；
6. `trace.json` **恰好覆盖**全部 REQ（缺一报错、多一报错），每条含 `files`/`verification`/`status`；
7. `status=done` 时，`tasks.md` 全部勾选 **且** trace 全部 `verified`（或 `waived`）。

`draft` 状态下第 4–6 项降级为**警告**。

---

## 6. 与既有设施的关系

| spec 元素 | 落到哪里 |
|---|---|
| EARS 的可执行验收 | `ccs.ps1 build` / `run-host-tests.ps1` / 上板自检清单 |
| 重大设计取舍 | `harness/docs/adr/NNNN-*.md`（spec 的 `meta.adr` 引用它） |
| 任务完成判据 | `AGENTS.md` §2 Definition of Done |
| 阶段快照 | `projects/<proj>/state.json` |

**spec ≠ ADR**：ADR 回答"为什么这么选"；spec 回答"要什么 / 怎么做 / 做到没"。

---

## 7. 契约文件

| 契约 | 约束对象 | 校验方 |
|---|---|---|
| `harness/schemas/spec.schema.json` | `specs/<id>/spec.json` | `spec.ps1 check`（内联实现） |
| `harness/schemas/trace.schema.json` | `specs/<id>/trace.json` | `spec.ps1 check` |
| `harness/schemas/state.schema.json` | `projects/<proj>/state.json` | `verify.ps1`（state-schema 腿） |
| `harness/schemas/project.config.schema.json` | `projects/<proj>/project.config.json` | `static-check.ps1`（config 腿：schema + 工程名/路径/输出目录交叉校验） |
| `harness/schemas/manifest.schema.json` | `harness/env/manifest.json` | `doctor.ps1`（写出后自校验，违约写进 `problems` 并退出 1） |

---

## 8. 参考范例

`specs/0001-led-chaser/` 是本环境第一份 spec（流水灯：pattern 引擎 + 极性映射 + 节拍 + 上电自检），
含完整 EARS 句、需求覆盖表、任务粒度与追溯填写方式。
