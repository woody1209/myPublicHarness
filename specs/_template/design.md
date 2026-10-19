# design.md — <特性名称>

> 状态：`draft` ｜ 上游：`requirements.md`

## 1. 概述

<一段话：方案要点与为什么这么选。>

## 2. 架构与分层

```
app / main
  ↓
<module>（纯逻辑，source/<module>.c —— 只依赖 <stdint.h>/<stdbool.h>/<stddef.h>）
  ↓
<module>_hw（硬件相关，driverlib / 寄存器）
```

符合 AGENTS.md §4 与 `harness/docs/30-编码与单测规范.md` 的分层铁律：纯逻辑模块进 `source/`，
不 include 任何 driverlib 头文件，因此可在主机侧编译测试。

## 3. 组件与接口

| 组件 | 文件 | 职责 | 平台相关 |
|---|---|---|---|
| `<module>` | `projects/<proj>/source/<module>.c` | <职责> | 否 |
| `<module>_hw` | `projects/<proj>/source/<module>_hw.c` | <职责> | 是 |

### 接口契约

```c
/* source/<module>.h */
<原型>
```

- 返回码语义：<...>
- 调用方契约：<...>

## 4. 数据与内存

- 静态数据：<...> —— RAM 占用 <N> 字节
- Flash 占用：<估算>
- 无动态内存（符合 AGENTS.md §4）。

## 5. 时序与并发

- 执行上下文：<主循环 / ISR / RTOS 任务>
- 可重入性：<内部是否有状态，如何传递>
- 关键阈值：<必须在多少时间内完成 / 节拍误差多少>

## 6. 错误处理

| 场景 | 处理策略 | 返回/表现 |
|---|---|---|
| <边界输入> | <策略> | <结果> |
| <硬件失败> | <策略> | <结果> |

## 7. 验证策略

- **主机侧单测**：`harness/tests/host/test/test_<module>.c`，用例：<列出>
- **目标侧**：上板自检 + `harness/tests/on-target/README.md` 清单

## 8. 需求覆盖表

| 需求 | 设计落点 | 验证手段 |
|---|---|---|
| REQ-001 | §3 接口契约、`source/<module>.c` | <测试/命令> |

## 9. 风险与取舍

| 风险/取舍 | 影响 | 缓解 |
|---|---|---|
| <取舍> | <影响> | <缓解措施> |

---

### 检查清单

- [ ] 架构分层符合铁律（纯逻辑无硬件依赖）
- [ ] 接口契约明确
- [ ] 内存/Flash 预算有估算
- [ ] 需求覆盖表覆盖全部 REQ
