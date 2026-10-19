# env — 环境快照

- `manifest.json`：由 `doctor.ps1` 生成的机器可读环境快照（CCS 版本/路径、C28x 编译器、
  C2000Ware、SysConfig、gmake、probe、工作区资产、问题列表）。**每次会话开始先刷新**。
- `AGENTS.md` 规则 1：manifest 失效或 doctor 报 FAIL 时，先修环境再干活；
  不要凭记忆假设 CCS / 编译器 / C2000Ware 路径。

手动刷新：

```powershell
.\harness\tools\doctor.ps1
```

manifest 关键字段：

| 字段 | 含义 |
|---|---|
| `ccs.root/version` | CCS 安装根目录与版本 |
| `toolchain.c2000` | cl2000.exe 路径与版本（`--compiler_revision`） |
| `sdk.c2000ware` | C2000Ware 根目录与版本号 |
| `tools.gmake/sysconfig/loadti/xdsdfu` | gmake、SysConfig CLI、loadti、xdsdfu 路径 |
| `tools.cmake/host_cc` | 主机侧单测工具链 |
| `probes` | `xdsdfu -e` 枚举到的 XDS110（含序列号与固件版本） |
| `workspace.app_dirs` | 含 Makefile 的工程列表 |
| `problems[]` | 阻塞性问题（非空时修复后再继续） |

## 工程配置（分层配置机制）

- `project.config.example.json`：工程级配置的**示例/模板**（契约
  `harness/schemas/project.config.schema.json`）。
- 实际工程配置放在 `projects/<proj>/project.config.json`（如
  `projects/launchxl-f280039c-led-chaser/project.config.json`）。
- 分层加载：`harness/env/` 的全局默认 ← 工程配置覆盖（深合并）。
  工具只**读**配置，不写配置。
- 运行时状态快照另存 `projects/<proj>/state.json`（契约
  `harness/schemas/state.schema.json`），记录各阶段最近一次结果。
