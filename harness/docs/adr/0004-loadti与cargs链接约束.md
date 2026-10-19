# ADR 0004 · loadti 加载与 `__c_args__`：链接必须带 `--args=`

- 状态：已接受
- 日期：2026-10-17
- 决策者：f280039c-harness 项目

## 背景

2026-10-17 05:46 用 `test-loadti.ps1` 验证 `ccs.ps1 program` 烧录路径，loadti 报：

    SEVERE: __c_args__ not defined.  Use "--args=n" when linking target
    executable to allocate n bytes for arguments.
    Error code #4011, ...led-chaser.out load failed!

同一镜像在 HIL（dss.bat → `memory.loadProgram(file)`）里 H2 屡次 PASS——
两条加载路径对同一镜像的要求不同，此前"program 可用"的结论未经端到端验证。

## 决策

1. **Makefile LDFLAGS 固定带 `--args=0x200`**：定义 `__c_args__`（argc/argv 存储，
   512 字节放 RAM，`.args` 段）。取值依据：loadti 至少写入 argv[0]=.out 绝对路径
   （本机约 90 字符 × 2 字节/字 ≈ 180B）+ argv 指针数组，0x200 留足余量；
   RAMLS0 余量 >2KB，无压力。
2. **`.out` 目标增加 `Makefile` 依赖**：否则改 LDFLAGS 不触发重链，旧镜像照烧
   （本次排查中该陷阱真实发生：改完 Makefile map 时间戳未变）。
3. **两条加载路径的差异写入文档**（docs/40 DSS 经验 + 故障表）：
   `memory.loadProgram(file)` 不带 argv → 不需要 `__c_args__`；
   loadti 内部 `loadProgram(file, argvArgs)` → 必须有。
4. **自检方法**：map 中 `__c_args__ = abs ffffffff` 即未定义（修复前实证）；
   修复后 `__c_args__` 有实体地址与 0x200 尺寸。

## 证据链

| # | 证据 | 结论 |
|---|---|---|
| 1 | loadti 源码 `main.js:243`：`debugSession.memory.loadProgram(outFile, testEnv.argvArgs)` | loadti 带 argv 加载，需要 `__c_args__` |
| 2 | `getArgs.js`：`argvArgs[0] = outFileName` | 至少写入一个路径字符串，.args 不能为 0 |
| 3 | 修复前 map：`abs ffffffff __c_args__` | 符号未定义，loadti 4011 |
| 4 | 修复后（--args=0x200）重链成功，map：`00000128 __c_args__`，size 报告 `.args 512 0x200` | 符号已定义，镜像可被 loadti 加载 |
| 5 | HIL 全程 `loadProgram(file)` H2 PASS（修复前后均如此） | HIL 路径本就不依赖 `__c_args__`，回归风险为零 |

## 备选与理由

| 备选 | 落选原因 |
|---|---|
| 让 loadti 不带 argv 加载（改 loadti.bat/脚本） | 改动 CCS 安装目录内的官方脚本，升级即被覆盖，且属 harness 外资产 |
| `--args=0` | loadti 至少写 argv[0]（.out 路径），0 字节有溢出风险 |
| 只修 Makefile 不加文档 | 4011 症状与 HIL 无关，下次换工程/换人必再踩；两条路径差异必须成文 |

## 后果

- `ccs.ps1 program`（loadti 烧录路径）恢复可用；待板重连后与 HIL 一并端到端验证。
- 新工程从参考工程拷 Makefile 时自动继承 `--args=0x200`（Makefile 头部注释已说明来历）。
- 成本：RAM 512 字节（.args）；`.text` 不变。
- 触发复审：若未来弃用 loadti 改纯 DSS 加载，或 main 改为接收命令行参数，
  需重新评估 `--args` 尺寸并更新本 ADR。
