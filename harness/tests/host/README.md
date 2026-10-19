# 主机侧单元测试（Host Unit Tests）

编译**纯逻辑**固件模块（不依赖任何外设/寄存器），用主机编译器运行，是"不上板也能证明正确性"的那一层。

## 运行

```powershell
.\harness\tools\run-host-tests.ps1
```

驱动优先级：
1. **cmake + ctest**（preferred，CI 用同一条路径）
2. **MSVC `cl.exe`**（cmake 缺失时的回退；本仓库要求离线也能跑回归）

回退实现刻意**不调用 `vcvars64.bat`**（它依赖 `reg.exe`，常被安全策略拦截），
而是直接按 MSVC tools + Windows Kits 的已知路径拼装 `INCLUDE`/`LIB`/`PATH`。

结果同时落盘到 `harness/tests/host/build-host-msvc/last-test-log.txt`
（主机控制台不转发子进程输出时也能取证）。

## 测试框架

`harness/tests/host/framework/fw_test.{h,c}` —— 自带、零依赖、离线可跑
（C3M3-harness 走 Unity + FetchContent，需要网络与 cmake；本分支改为自持以保确定性）。

断言宏：`FW_ASSERT_TRUE` / `FW_ASSERT_FALSE` / `FW_ASSERT_EQ_I32` /
`FW_ASSERT_EQ_U32` / `FW_ASSERT_EQ_STR`。

## 规则

1. 进组的固件源码必须只依赖 C 标准库 + 本仓库头文件——检查方法：删掉所有
   driverlib / 寄存器访问后仍能编译。
2. 硬件相关代码放 `*_hw.c` / `board_*.c`，本层不可达。
3. 新增模块必须同时新增 `test/test_<module>.c`，并在 `CMakeLists.txt`
   **与** `run-host-tests.ps1` 的源码列表里注册（两套驱动必须同时维护）。
4. **目标侧上电自检必须与主机侧共用同一组参考常量**（`led_pattern.h`）；
   `self_test.c` 本身就是纯逻辑模块，主机侧会直接执行它。

## 当前覆盖

| 模块 | 测试文件 | 用例要点 |
|---|---|---|
| `led_pattern` | `test/test_led_pattern.c` | CHASE/BOUNCE/BLINK 三模式序列与回绕、n=1/4/32 边界、非法通道数返回 0、active-low 极性映射、FNV-1a 参考常量 |
| `fmt` | `test/test_fmt.c` | 十进制（0 / UINT32_MAX）、十六进制格式与前缀、缓冲不足返回 -1 且不越界、刚好放下的边界 |
| `self_test`（目标自检本身） | `runner/test_runner.c` 中调用 | `self_test_run() == SELF_TEST_OK` |

## CI

GitHub Actions 模板见 `../templates/ci/github-actions.yml`（host-tests job）。
