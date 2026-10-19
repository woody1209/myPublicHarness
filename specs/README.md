# specs/ — 特性规格（Spec 驱动开发）

本目录采用 **kiro 风格的 spec 模式**（与 `C3M3-harness` 同构）：任何**非平凡**的固件改动，
先把"要做什么"写清楚，再写"怎么做"，最后拆成"可勾选、可验证的任务"，然后才动代码。

> 设计取舍：借鉴"rule → agent → rule"三段式执行模型、契约化（JSON Schema）、
> 需求可追溯（REQ → 文件/测试/验证命令）；形式上采用 kiro 的三件套
> （requirements / design / tasks）+ 批准门。

---

## 一、什么时候必须写 spec

| 级别 | 例子 | 要求 |
|---|---|---|
| **免 spec** | 改注释、改日志文案、单文件小修 | 直接改，走 AGENTS.md §2 DoD |
| **必须 spec** | 新模块 / 新外设驱动 / 协议实现 / 时钟策略变更 / 引脚分配变更 / 跨文件重构 / 引入 RTOS / 任何"需要用户拍板取舍"的事 | 先写 spec，`requirements` 与 `design` 经用户确认后再实现 |

判定口径：**改动会引入新的对外行为或新的硬件依赖 → 写 spec。**
拿不准时按"必须 spec"处理（写 spec 的成本远低于返工）。

---

## 二、三件套与流程（kiro 模式）

```
specs/<NNNN-slug>/
├── requirements.md   ← ① 需求：用户故事 + EARS 验收标准（REQ-XXX）
├── design.md         ← ② 设计：架构 / 接口 / 时序 / 内存 / 风险
├── tasks.md          ← ③ 任务：可勾选清单（T-XXX，标注关联 REQ 与验证命令）
├── spec.json         ← 机读镜像（requirements 的结构化副本，供工具校验/追溯）
└── trace.json        ← 追溯矩阵（REQ → 文件 / 测试 / 验证命令 / 状态）
```

流程与批准门：

```
① requirements.md  ──[用户确认]──▶  ② design.md  ──[用户确认]──▶  ③ tasks.md
                                                                     │
                                                          按 tasks 实现 ──▶ 勾选 + 填 trace.json
                                                                     │
                                                          spec.ps1 check 通过 = 完成
```

**硬规则**：

1. **未获用户确认，不得越过 requirements/design 阶段直接写代码。**
   （批准记录写在 `spec.json` 的 `meta.status` 与各文件顶部的状态行。）
2. **每条 REQ 至少有一条 EARS 验收句和一条可执行验收**（命令或断言），否则视为需求未写清。
3. **每条 REQ 至少被一个 task 覆盖**；task 完成后必须把文件/测试/验证命令填进 `trace.json`。
4. **代码与 spec 双向一致**：实现中若发现需求需变，先改 spec 并重新确认，再改代码。

---

## 三、REQ / TASK 编号与字段

- 需求编号 `REQ-001` 起，顺序不跳号；任务编号 `T-001` 起，顺序不跳号。
- 优先级：`must` / `should` / `could`；类别：`functional` / `interface` / `performance` / `constraint`。
- 字段规范见 `../harness/schemas/spec.schema.json`。

### EARS 验收句式（需求可判定化的关键）

| 类型 | 模板 | 例 |
|---|---|---|
| 普遍 | THE SYSTEM SHALL `<行为>` | THE SYSTEM SHALL 把 LED4 映射到 GPIO20、LED5 映射到 GPIO22 |
| 事件驱动 | WHEN `<触发>`，THE SYSTEM SHALL `<行为>` | WHEN 节拍计数器由 k 递增到 k+1，THE SYSTEM SHALL 将点亮位从 idx 移到 idx+1 |
| 状态驱动 | WHILE `<状态>`，THE SYSTEM SHALL `<行为>` | WHILE 自检通过，THE SYSTEM SHALL 以 200ms 步进推进流水灯 |
| 异常处理 | IF `<条件>`，THEN THE SYSTEM SHALL `<行为>` | IF 自检失败，THEN THE SYSTEM SHALL 进入双灯快闪的 fail-safe 形态并不再推进 pattern |
| 可选特性 | WHERE `<特性启用>`，THE SYSTEM SHALL `<行为>` | WHERE 配置为 CPU1_RAM，THE SYSTEM SHALL 从 RAM 链接并执行同一份源码 |

EARS 句只描述**可观测、可判定**的行为；"应该尽量快"这类模糊表述必须落到具体阈值。

---

## 四、与既有设施的关系

| spec 阶段 | 落到的既有设施 |
|---|---|
| requirements 的验收命令 | `harness/tools/ccs.ps1` / `run-host-tests.ps1` / 上板自检清单 |
| design 的架构取舍 | `harness/docs/adr/NNNN-*.md`（重大取舍必须留 ADR，spec 里引用） |
| tasks 的完成判据 | `AGENTS.md` §2 Definition of Done |
| 运行时状态 | `harness/schemas/state.schema.json` 定义的 `projects/<proj>/state.json` |

---

## 五、用法

```powershell
.\harness\tools\spec.ps1 new -Id 0002-uart-bridge -Title "UART 桥接"
.\harness\tools\spec.ps1 list
.\harness\tools\spec.ps1 check
.\harness\tools\spec.ps1 check -Id 0001-led-chaser
.\harness\tools\spec.ps1 trace -Id 0001-led-chaser
```

---

## 六、目录约定

- 每个特性一个目录，命名 `<4 位序号>-<kebab-slug>`（如 `0001-led-chaser`）。
- `_template/` 是模板目录，**不要**在里面填真实内容。
- 已完成的 spec **保留不删**——它是需求演进与回归的依据。
