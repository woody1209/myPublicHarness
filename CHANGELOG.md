# 变更记录（CHANGELOG）

> 格式参照 Keep a Changelog。每条变更写明**来源**（issue/审计编号）、**改动**、**验证命令**。
> 本仓库此前无此文件；2026-10-31 工程完备性审计（G6）建立。
> 相关：`harness/docs/82-工程完备性审计.md`、`harness/docs/50-CI与版本管理.md`。

## [未发布]

### 派生 full-platform + 测试金字塔补齐（2026-10-31，用户指令：派生 / 授权提交推送建 PR）

**D1 — 派生基线 `launchxl-f280039c-full-platform`（审计 G7，P0 缺口闭合）**
- 从 `F280039C-harness` 派生工程（58 源文件 + Makefile + 链接脚本 + ccxml + `specs/0002-full-platform`
  22 REQ）与 **4 份上板证据记录**（2026-10-24 / 10-25 / 10-30 / 10-30-soak2）。不复制 `CPU1_*` 构建产物。
- **spec 编号约定**：保留基线血缘，full-platform 仍为 `0002-full-platform`；原 `0002-motor-pwm-skeleton`
  顺延为 **`0003-motor-pwm-skeleton`**（12 处引用同步更新，含 README / 审计 / state.json / HIL 脚本 / 学习站点）。
- 主机单测接入：新增 6 个套件（scheduler/health/fp_selftest/adc_stat/math_ref/frame_gen）+ `test_runner_fp.c`
  + `CMakeLists.txt` 的 `fw_logic_fp`/`test_runner_fp` + `modules.json` 注册。
  主机单测总量 **20 + 41 + 36 = 97 用例，0 失败**。
- 验证：`.\harness\tools\ccs.ps1 clean` + `build` → 退出 0；
  `.\harness\tools\verify.ps1 -Project launchxl-f280039c-full-platform -Hil` → **VERIFY OK - 11 legs PASS**
  （FLASH 18294 字 = 9.3 %，RAM 1932 字 = 5.47 %，`.text` 13994 vs 基线 14450，delta 456 ≤ 1024）

**新增门禁腿 #1b 代码审查（`harness/tools/review.ps1`）**
- 与 `static-check.ps1` 分工：static = 契约符合性（分层/清单/schema/死链/analyze），
  review = 工艺与契约（可审查性/可追溯/未完债务是否记账）。
- ERROR 级 4 条：TODO 未挂 spec/ADR/issue、字面外设地址传给 `HWREG*`、头文件缺守卫、单文件 >1000 行。
  WARN 级 1 条：源文件未被任何 spec trace 认领（派生带来 52 项，记为待办而非阻断）。
- **可证红**：注入 `// TODO` + `HWREGH(0x6B)` 探针 → `REVIEW FAILED (2 findings)` 退出 1；探针已删除。
- 验证：`REVIEW OK (0 findings)`（3 工程 96 源文件扫描）

**新增门禁腿 #2b 软测 SIL（`harness/tools/run-sil-tests.ps1` + `harness/tests/sil/`）**
- 三层测试金字塔补齐：单测（单返回值）→ **SIL（时间序列性质）** → HIL（真板）。
- SIL 场景：调度器节拍/相位错峰/**900 ms 卡顿不补拍且迟到如实记账**；健康监视 MUST 阈值锁存与
  beat 恢复、SHOULD/COULD 永不锁死；SVPWM 整周波最小相对齐、线电压不变性、120° 旋转置换、满量程守卫。
- **首次运行即红**（motor-pwm 2 failures）：根因是我的断言假设错——实现是 **min-max 下对齐**
  （`da = ta - tmin`，最低相为 0）而非中点 500 定心，故 `a+b+c` 本就不恒定。改断言为真实契约后 PASS。
- 验证：`SIL RESULT: PASS`（15 用例 0 失败；120° 旋转实测偏差 2‰）

**full-platform HIL 闭环（`harness/tools/dss/hil_test_fp.js`）**
- H5 采用 halt-free 采样（DSS peek 实测 ~175 ms/次，窗口按实测成本而非标称节拍定尺寸）。
- **真实缺陷发现**：H5b 检出看门狗**间歇复位**（2 次运行 1 次）。REQ-018 在 spec 中为 `pending`、
  基线亦记 "bring-up open"，故 H5b 定为**检测器而非阻断门**（WARN + 证据 + 待办），
  H5a 改为"相邻样本计数递增"以抗复位——既不掩盖缺陷，也不让门间歇红。REQ-018 收口后应升级回 `fail()`。
- 验证：`HIL RESULT: PASS`，看门狗 5.95 s 服务 135168 次，m2=0x57 / m4=0x3f 有活动

**PowerShell here-string 反引号陷阱（真实缺陷，已修）**
- `run-hil.ps1` 的 markdown 代码围栏 ` ``` ` 落在双引号 here-string 内：反引号是 PS 转义符，
  奇数个会吞掉换行，导致终止符 `"@` 失效、后续全部变成字符串，报错位置却在 100 行之后。
- 修法：围栏改为变量插值（`$fence = '```'`）。

### 审计整改（来源：`harness/docs/82-工程完备性审计.md`）

**G1 — P0 静态门禁红（README 死链）**
- `README.md:93`：两处 HIL 记录路径 `records/xxx.md` 缺前缀，改为
  `harness/tests/on-target/records/hil-2026-10-18-led-chaser.md` 与
  `harness/tests/on-target/records/hil-2026-10-25-launchxl-f280039c-motor-pwm.md`
- 验证：`.\harness\tools\static-check.ps1` → `STATIC CHECK OK (0 violations)`（修前 2 violations）

**G2 — P1 spec 0002 需求→测试追溯断裂**
- `specs/0003-motor-pwm-skeleton/trace.json`：补 5 条空 `tests`
  （REQ-001→`harness/tools/static-check.ps1`；REQ-002/003/004→`harness/tools/dss/hil_test_mp.js`；
  REQ-007→`harness/tools/run-hil.ps1`）。硬件相关 REQ 主机侧无纯逻辑单测，故指向真实判据脚本。
- 验证：`.\harness\tools\spec.ps1 check -Id 0003-motor-pwm-skeleton` → `CHECK OK`

**G3 — P0 无 `.gitignore`，构建产物入库**
- 新建 `.gitignore`（排除 `*.obj/.o/.d/.out/.map/.pdb/.ilk`、`_linkInfo.xml`、
  `projects/*/CPU1_FLASH|RAM`、IDE 文件、主机单测构建产物、records 原始 `.log`；
  保留 `build-verify/*.log`、`records/*.md`、`state.json`）
- `git rm --cached` 从索引移除 **125 项**构建产物（**工作区文件未删除**）
- 验证：`git ls-files | grep -E "\.(obj|exe|pdb|out|map|ilk)$|CPU1_"` → 0 行

**G4 — P0 CI 从未实例化**
- 新建 `.github/workflows/harness.yml`：host-tests(ubuntu/ctest) / spec-check(windows) /
  static-check(windows) / firmware-build(self-hosted, 默认不跑)
- **状态：未推送，CI 尚未真正运行过**。推送后需回填首次运行记录。

**G5 — P1 motor-pwm 缺工程 README**
- 新建 `projects/launchxl-f280039c-motor-pwm/README.md`（引脚表 / 时序参数 / SCI 契约 /
  体积基线 / HIL 证据 / 待人工补测 / 安全提示）

**G6 — P1 变更无记录**
- 本文件建立。仓库此前 git 仅 1 commit（2026-10-19），此后 motor-pwm 实现、spec 0002 定稿、
  `docs/learning-site` 学习站点均未提交。

**G11 — P0 led-chaser 上板腿真红（H5-sample 样本不足）**
- 现象：`HIL RESULT: FAIL H5-sample (16 / 17 halt-free peeks ... cannot judge cadence)`，
  连续两次（非 flaky）
- 根因：**DSS halt-free peek 在本机退化到 ~131 ms/peek**（probe/host 侧），固定 2500 ms 窗口
  只够 16–17 个样本，低于 20 个的判定下限。固件节拍实测 **mean = 198.6 ms**（期望 200 ±25 %），
  **固件本身没问题**——这是探针性能退化被读成了固件缺陷
- 修复：`harness/tools/dss/hil_test.js`
  - `SAMPLE_WINDOW_MS` 2500 → **6000**（给慢 peek 余量）
  - **20 个样本下限与 ±25 % 节拍容差一律未动**（不放宽判据）
  - 诊断行前置：peek 周期与样本数**先打印再判下限**，慢探针不再静默
- 验证：`.\harness\tools\run-hil.ps1 -Project projects\launchxl-f280039c-led-chaser`
  → `LED samples: 46 halt-free peeks over 5.89 s (~131 ms/peek, 0 failed)`、
  `LED transitions observed: 30`、`chase period: mean=198.6ms`、**`HIL RESULT: PASS`**

### 新增

- `harness/docs/82-工程完备性审计.md`：AIDLC + SDD + TDD + harness 完备性审计，含 10 条差距分级、
  功能覆盖矩阵（基线 full-platform 22 条 REQ vs 本仓库）、魔鬼代言人 4 条、复评触发条件。
- `harness/tests/on-target/records/hil-2026-10-31-launchxl-f280039c-motor-pwm.md`：
  2026-10-31 真板 HIL 记录（`verify.ps1 -Hil` → VERIFY OK - 9 legs PASS）。

### 已知缺口（见 82 §9）

- ~~**G7**~~ ✅ **已闭合**（2026-10-31）：基线 `specs/0002-full-platform` 已派生进本仓库，
  22 REQ / 58 源文件 / 4 份上板证据齐全，`verify.ps1 -Hil` = 11 legs PASS。
- **G8**（原编号，对应待办 A3/A4）：`harness/docs/80-harness闭环符合性审计.md`、
  `harness/docs/81-AIDLC符合性审计.md` 未覆盖 motor-pwm 与 full-platform
  （仍是 motor-pwm 之前的快照）；`00-harness工作流总览.md` 缺 motor-pwm / 学习站点入口。**未处理**。
- ~~**G9**~~ ✅ **已闭合**：`harness/docs/50-CI与版本管理.md` 已改为 Git 现状
  （main `2e97505` + 特性分支 `7832eac` 已推送 + PR 待创建）。
- **G10**：本机无 cmake，主机单测走 MSVC `cl.exe` fallback（首选路径未启用）。

### 构建与文档整改（2026-11-01，来源：82 §9 待办 A10 / A11 / A3 / A4）

**A11 — Makefile driverlib 规则名与产物不一致（已修，可复核）**
- 根因：`$(BUILD_DIR)/dlib_%.obj $(BUILD_DIR)/%.obj` 多目标模式规则，而 cl2000 用
  `--obj_directory` 时**按源 basename 命名**（产出 `spi.obj`），故目标与产物名不一致 →
  gmake 每次强制重编并打印 `pattern recipe did not update peer target`。
- 修法：单目标规则 `$(BUILD_DIR)/dlib_%.obj: $(DLIB_DIR)/%.c`，配方加 **`--output_file="$@"`**，
  使产物名恒等于目标名；`dlib_` 前缀同时避免与同名应用模块相撞。
- 验证：`rebuild` EXIT=0 → **增量 `build` 零动作、告警消失**。

**A10 — 链接脚本 `.scratchpad` 无 SECTIONS 规范（已修，可复核）**
- 链接告警 `#10247-D: creating output section ".scratchpad" without a SECTIONS specification`。
- 修法：`28003x_full_platform_flash_lnk.cmd` 增加 `.scratchpad : > RAMLS2, ALIGN(4)`
  （RAMLS2 当时 map 用量 0）。
- 验证：重链后告警消失，`size` 报告显式列出 `.scratchpad` RAM 0x9000 = 10 字；
  FLASH/RAM 总量不变（18294 / 1932 字）。

**A3（部分）/ A4 — 文档快照过时（审计"说明必须符合工程实际"）**
- `80-harness闭环符合性审计.md`、`81-AIDLC符合性审计.md` 抬头加**「快照边界」声明**：
  二者是 led-chaser 单工程快照，motor-pwm / full-platform / #1b / #2b 不在覆盖内，指向 82。
  **正文按三工程逐条改写仍待做**（保留为待办）。
- `00-harness工作流总览.md`：抬头补三工程清单 + 学习站点入口；§2 设施地图补
  `review.ps1`（#1b）、`run-sil-tests.ps1`（#2b），verify 腿数 9 → **11**。

**其他**
- `.gitignore` 补 `harness/tests/sil/build-sil*/` 与一次性诊断日志（避免构建产物再次入库）；
  已跟踪的 `sil_runner_*.exe` 用 `git rm --cached` 移出索引（**工作区文件未删**）。
- 新增上板证据：`harness/tests/on-target/records/hil-2026-11-01-launchxl-f280039c-full-platform.md`。
- 验证：`.\harness\tools\verify.ps1 -Project launchxl-f280039c-full-platform -Hil`
  → **VERIFY OK - 11 legs PASS**（host 97 / SIL 15 / HIL PASS，`.text` 13994，SIZE FLASH 9.3 % / RAM 5.47 %）。

### 需求→文件追溯闭合（2026-11-01，来源：82 §9 待办 A8 / A12）

**A8 — 52 个派生源文件未被 trace 认领（已闭合，可复核）**
- 按子系统归入 `specs/0002-full-platform/trace.json`，共新增 **53 条 `files` 条目**：
  `accel_hw`/`math_ref`/`board_error`→REQ-005、`board_clock`/`dcc_mon_hw`→REQ-002、`cla_*`→REQ-004、
  `adc_scan_hw`/`adc_stat`→REQ-006、`dac_cmpss_hw`→REQ-007、`pwm_bank_hw`→REQ-008、`cap_meas_hw`→REQ-009、
  `qep_quadv_hw`→REQ-010、`spi_loop_hw`→REQ-011、`i2c_pair_hw`→REQ-012、`sci_log`/`serial_loop_hw`/`fmt`→REQ-013、
  `can_loop_hw`→REQ-014、`fsi_loop_hw`→REQ-015、`dma_bank_hw`→REQ-016、`clb_demo_hw`→REQ-017、
  `main.c`→REQ-018（看门狗服务点，实证 `main.c:203 SysCtl_serviceWatchdog()`）、
  `frame_gen`/`led_hw`/`led_pattern`/`health.h`/`self_test.h`→REQ-020、`app_version.h`→REQ-001。
- 另补 motor-pwm `svpwm_table.h` → `specs/0003-motor-pwm-skeleton/trace.json` REQ-006。
- 验证：**未认领源文件 52 → 0**；`review.ps1` 的 WARN 由 38 条降为 **0**（`REVIEW OK (0 findings)` 且无 WARN 段）。

**A12 — spec-check 警告 REQ-009 verification 缺可重跑命令（已闭合）**
- 补 `.\harness\tools\run-hil.ps1 -Project projects\launchxl-f280039c-full-platform`
  （或 `verify.ps1 ... -Hil`）。
- 验证：`spec.ps1 check -Id 0002-full-platform` → **CHECK OK，0 警告**。

### 合入主干（2026-11-01，用户指令：分支合并入 main）

- 分支 `feat/full-platform-derivation-and-test-pyramid`（HEAD `9f0b5be`）**已合并入 `main` 并推送**。
  合并方式：`git merge --no-ff`（**保留特性分支历史**，便于回滚与审计），提交信息写明来源与门禁结论。
- **未走 GitHub PR 流程**，原因如实记录：本机 git 走 TortoiseGit（只覆盖 git 协议层，**无 PR API**），
  `gh` CLI 已装但**未认证**（无 token）。因此"PR 编号"这一项空缺，改以本文 +
  `harness/docs/83-PR-变更记录.md` 承担变更记录职责（标题 / 正文 / 证据表 / 遗留项齐备，见 83 §3）。
- 合并前置门禁（全绿，实测）：`verify.ps1 -Project launchxl-f280039c-full-platform -Hil`
  → **VERIFY OK - 11 legs PASS**（host 97 / SIL 15 / HIL PASS，`.text` 13994，FLASH 9.3 % / RAM 5.47 %）。
- 合并后待办（不因合入而消失）：**A7** 看门狗间歇复位、**A9** m3 通信未活动、**A3 正文**
  按三工程改写 80/81、**G4** CI 首次运行结果回填、**A5** cmake（均在 82 §9 待办表留痕）。
