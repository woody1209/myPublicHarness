# 目标机上板自检清单（On-Target Acceptance Checklist）

主机单测覆盖不了的硬件行为，烧录后按此清单执行并留存证据（SCI 日志 / LED 形态）。

前置：USB 连 XDS110 → 板 LED0/LED1 电源灯常亮 → JP1/JP2 Shunts 已安装 →
串口工具打开虚拟 COM（115200-8N1）→ `.\harness\tools\ccs.ps1 probe` 能看到 XDS110。

## 冒烟（每次烧录后必做）

- [x] 串口出现启动 banner：目标 MPN、板型、固件版本、SYSCLK=120MHz、构建时间
- [x] 日志出现 `self-test: pattern OK`
- [x] 日志出现 `self-test: fmt OK`
- [x] LED4（红，GPIO20）与 LED5（绿，GPIO22）按 pattern 节拍轮流点亮
      —— 即流水灯效果（默认 200ms 步进；HIL H5 halt-free 实测 195.0ms 均值，同日四轮 195.0–197.0ms（散布 1.0 %），
      判据是 200ms ±25%，仪器级 ±2% 见下"极性/时序专项"）
- [x] 约每 5s 出现一次 `alive, steps=... , mode=...` 心跳计数
- [x] `.\harness\tools\ccs.ps1 size` 分区预算达标（门禁阈值：flash ≤ 90 %、RAM ≤ 90 %，超出即 exit 1）
      （2026-10-18：.text 3881 words / FLASH 5067 words = 2.58 % / RAM 1038 words = 2.94 %）
      —— map 的 length 列是 **字** 不是字节，1 word = 2 bytes（ADR 0002）；`.text` 相对
      `state.json build.size.text_words` 基线的增量 > 2KB(=1024 words) 由 `verify.ps1` 判红。

  以上 6 项证据（最新一次真实执行，`verify.ps1 -Hil` 9 腿全 PASS）：
  `records/hil-2026-10-18-led-chaser.md` +
  `records/hil-sci-2026-10-18_100726.log`（115200-8N1，XDS110 "Application/User UART"=COM5）；
  历史轮次留在 `records/hil-2026-10-17-led-chaser.md` 与同日 `hil-dss-*.log`。

## 自动 HIL（run-hil.ps1，替代人眼判读）

```powershell
.\harness\tools\run-hil.ps1            # 串口窗口默认 90s
```

DSS 脚本 `tools/dss/hil_test.js` 自动完成 H1-H6（JTAG / flash / 自检 / 主循环 PC /
LED 极性与节拍 / 留板独立运行），证据写入 `records/hil-<日期>-led-chaser.md` 并刷新
`projects/<proj>/state.json` 的 `hil` 段。编写判读外设的 DSS 脚本时注意：

- **C28x 字地址模型**：`memory.readWord()` 只返回一个 16 位字；32 位寄存器
  （如 GPADAT@0x7F00）必须读全两个字再拼接。
- **LED 位在 GPADAT 高字 0x7F01**（LED4=bit4、LED5=bit6）；低字 0x7F00 是
  GPIO0-15，未用引脚悬空有噪声（上拉默认关闭），不能用来判 LED。
- **判读只认 GPADAT_R@0x7F80**：GPADAT 输出锁存器的调试器 peek 会带悬空模拟噪声
  （boot ROM 保持默认时同样出现 0x3A77 类花值），要求"锁存==引脚"曾造成 14/28 假不匹配，
  现仅作信息打印。
- **外设时钟门控**：F28003x 复位后 CPUSYS `PCLKCRn` 全关，被门控外设的**写会被静默丢弃**
  （无错误码）。H3 因此读 PCLKCR7@0x5D330 bit0（SCIA）做守卫；CPUSYS 基址 0x5D300，
  `SYSCTL_O_*` 偏移按字直接相加。
- **时序必须 halt-free 测**：在核心运行状态下 peek 寄存器，用"远快于步进的采样 +
  平均周期"（首尾跳变跨度/(跳变数-1)）。**不要**用"halt—读—run"循环：TIF 是单标志，
  halt 期间的多次溢出恢复后只累加一次，实测把 200ms 步进报成 426ms；
  CPUTIMER0 时基是 SYSCLK（不是 LSPCLK/SYSCLK/4）。
- 读**内存/符号**前必须确认目标已 halt（`isHalted()` 轮询）：`target.halt()` 是异步的，
  核心仍在跑时读外设报 0x20000——与 GEL 映射未关同码不同因。（寄存器 peek 可在运行态做）
- `g_self_test_code==0` 不能单独证明自检通过（静态初值即 0）：必须与 H5 节拍
  采样（证明主循环在跑）+ SCI banner 组合判读；H3 的读取应放在采样窗口之后。
- 串口窗口必须覆盖 dss.bat 启动+连接+烧录（20-60s），banner 出现在固件首次运行
  与 H6 重引导两次。
- **探针在线时 `target.reset()` 不会引导 flash**：boot ROM 看 RESC@0x5D380 bit31 DCON 走
  Emulation Boot，仿真引导键无效即落 Wait/SCI（实测 PC 恒停 0x3FB8B9、灯不动）。
  H6 用 `PC = code_start` + `runAsynch` + `disconnect` 作等价冷启动（ADR 0006）。

## 极性/时序专项

- [x] **极性**：单亮 LED4 时测量 GPIO20 为低电平（≈0V），另一颗为高电平（≈3.3V）
      —— HIL H5 以 GPADAT_R 逐样本断言"任一时刻仅一灯为低"（2026-10-18 真机 23 样本全满足）
- [ ] **节拍**：用示波器/逻辑分析仪测 GPIO20 上升沿间隔 = 默认 200ms（±2%）
      —— HIL 的 JTAG peek 分辨率给不出 ±2% 结论，需仪器补测
- [ ] 修改 `step_ms` 后节拍随之线性变化（验证 config λ）

## 失败路径（Fault Injection）

- [x] 故意改坏 `LED_PATTERN_REF` 常量 → 上电后进入 **fail-safe**（双灯同亮同灭），
      `g_self_test_code=1`（REQ-004 已实测，trace.json 记为 verified）
      —— 2026-10-18 执行：常量改 `0xDEADBEEFU` → 主机侧 2 项断言失败、上板
      `GPADAT_R` 在 `0x0` ↔ `0x500000`（bit4+bit6 同亮）之间交替即 fail-safe；
      证据 `records/fault-injection-2026-10-18-led-chaser.md`，常量已还原并复跑 HIL PASS
- [ ] （回归时）再做故障注入**必须先确认坏常量真的进了镜像**：
      `dis2000 <.out>` 反汇编后 grep 该常量。改头文件**不会**自动触发重编，
      且"gmake 打印 building file"不等于 `.obj` 被重写 —— 见 **ADR 0007**
- [ ] 拔掉 XDS110 USB → CPU 继续运行（SCI 轮询发送不阻塞）
- [ ] 掉电上电 → banner/自检/心跳顺序与首次上电一致
      —— **只能人工做**：探针在线时 ROM 不会交接给 flash（见上），断电重启是唯一
      Stand-Alone Boot 观测窗口

## 记录模板

```
日期 / 操作者 / 固件版本 / .text 大小 / probe / 结果(PASS/FAIL) / 串口日志摘录 / 备注
```

- 2026-10-17 / Qoder agent + woodywu 供板 / 1.0.0（build Oct 17 2026）/ .text 3881 words
  （当时报告误标为 "B"：map 的 length 列是字，1 word = 2 bytes，ADR 0002）/
  XDS110 SN=CL391078 FW=3.0.0.43（cJTAG 2-pin，LaunchPad ccxml）/ **PASS**（H1-H6 +
  `ccs.ps1 program -Yes` loadti 烧录 exit 0）/ banner + sysclk 120000000 + self-test×2 OK +
  每 5s `alive, steps=25` / 本轮修复：SCIA 时钟门控（ADR 0005）、CPUTIMER RUNFREE 与
  halt-free 测量（ADR 0005）、H5 判读改 GPADAT_R；未覆盖：仪器级 ±2%、fail-safe 注入、断电重启。
- 2026-10-17（复跑收口）/ 同板同固件 / rebuild + `program -Yes` + `run-hil -ComPort COM5` ×2 /
  **PASS**：节拍 193.5ms 与 195.1ms（各 12 跳变）、PC 恒在应用 .text、PCLKCR7.SCIA=1、
  SERIAL-CAPTURE-OK；.text 3881 words 不变（仅注释修订）。同日 HIL 共 16 份运行日志
  （3 dss 崩溃 / 9 FAIL / 4 PASS），全部留在 `records/hil-dss-2026-10-17_*.log` 可追溯。
- 2026-10-18 / Qoder agent（无人值守跑完整门禁链）/ 同板同固件 /
  `probe`（Found 1 device）→ `build` → `program -Yes`（program OK, exit 0）→
  `verify.ps1 -Hil` → **VERIFY OK - 9 legs PASS**；HIL `HIL RESULT: PASS`，H1-H6 全过，
  节拍 mean=**195.0ms**（23 个 halt-free 样本 / 12 次跳变，判据 200ms ±25%；
  同日连跑四轮 197.0 / 195.8 / 196.5 / 195.0ms，散布 1.0 %，四轮原始 dss 日志都在 `records/`：
  `hil-dss-2026-10-18_094502.log`、`_095934.log`、`_100243.log`、`_100726.log`）、
  `g_self_test_code=0`、PC=0x82C53（应用 .text 内）、PCLKCR7.SCIA=1、
  串口 90s 窗口 `SERIAL-CAPTURE-OK`（banner + sysclk 120000000 + step 200ms +
  self-test pattern/fmt OK + `alive, steps=400…675` 12 行心跳）；
  size 门禁 FLASH 5067 words=2.58 % / RAM 1038 words=2.94 %，`.text` 与基线 delta=0。
  证据 `records/hil-2026-10-18-led-chaser.md`（含 dss/sci 原始日志路径）；
  仍未覆盖：仪器级 ±2% 节拍、断电冷启动观察（需人工）。

## motor-pwm（launchxl-f280039c-motor-pwm / spec 0003-motor-pwm-skeleton）

DSS 脚本 `tools/dss/hil_test_mp.js` 自动完成 H1-H6（JTAG / flash / 自检 / 寄存器回读 /
强制越限 trip + 自主恢复 / 留板独立运行）。证据写入
`records/hil-<日期>-launchxl-f280039c-motor-pwm.md`。**无功率板、无电机**——本工程是
开环角度→SVPWM→三相互补 PWM 的算法与波形骨架，只观察算法与 PWM 形态。

### 自动 HIL 已覆盖（H1-H6 全 PASS，2026-10-25）

- [x] **banner/自检**：`=== LAUNCHXL-F280039C motor-pwm ===` + `self-test: svpwm/angle/fmt OK`
- [x] **PWM 寄存器回读**：`pwm: prd=6000 db=120 pol=2 OK`；H5a 读 ePWM1/2/3
      `TBPRD=6000 DBRED=DBFED=120 DBCTL=0xB TZSEL=0x8000`（10 kHz 载波 / 1µs 死区）
- [x] **CMPSS 双向探针**：`cmpss: adc=<基线> hi=1 lo=0 OK`
      —— `hi`/`lo` 打印的是**比较器实际状态**（trip 方向=1 跳、免跳方向=0 不跳），
      判定用 `board_cmpss_wait_status` 的**匹配布尔** `hi_ok && lo_ok`（见下"契约语义"）
- [x] **CMPA 真在跑**：H4 统计 26–27 个 distinct CMPA 值（max=6000=PRD），证明 SVPWM
      未停在零矢量；`motor:` 行 `a=/b=/c=` 占空比随电角度 10 Hz 更新
- [x] **ADC 链**：`motor:` 行 `adc=37..40` 随 A2 电压变化（非 0xFFFF=超时）
- [x] **CMPSS→XBAR→DCBH→DCBEVT1 一次性 trip 链**：强制 DAC 越限→COMPSTS=1→三模块
      TZFLG bit5 锁存→GPADAT_R 六路输出全低（GPIO0-5=0）；恢复 DAC=4095→固件 5 s 驻留后
      自清锁存并恢复调制（CMPA 寄存器重新变化）`FAULT: tz-latched`→`tz: cleared`
- [x] **SCI 九项契约**：banner / sysclk=120MHz / rate / mag / pwm-OK / cmpss-OK /
      tz-armed / motor-line / alive 心跳
- [x] **故障只触发一次**：无重复 `FAULT` 行，恢复后 `motor:` 续跑

### 契约语义（易错，务必区分）

- **`cmpss:` 行的 `hi=`/`lo=` 是比较器状态值**（`board_cmpss_comp_status()`），
  而**判定**用的是 `board_cmpss_wait_status(expect)` 的返回值——它返回"状态是否匹配
  expect"**而非状态本身**。trip 方向 `wait(true)` 命中 → `hi_ok`；免跳方向 `wait(false)`
  命中 → `lo_ok`；探针通过 = `hi_ok && lo_ok`。曾误写成 `hi && !lo` 导致 `lo=1` 假 FAIL。
- **CMPSS lo 方向参考用满量程 `BOARD_CMPSS_DAC_MAX`**（不是 `isense+512`）：A2 悬空 +
  ePWM 载波耦合噪声会超过 512 码窗口；满量程参考物理上界高于任何引脚电压，方向确定。
- **`H5-trip-resume` 判"恢复"看 CMPA 寄存器变化，不能用瞬时 GPADAT_R**：中心对齐
  up-down + AQ（ZRO→高、UP_CMPA→低、DOWN_CMPA→高）下，某相占空比=PRD=1000 时该相
  A/B 两脚**整周期都是低**（两端谷+死区覆盖全周期），瞬时读全零属正常。CMPA 真变化
  才是"调制恢复"的可靠信号。

### 待人工补测（仪器/断电，自动门禁覆盖不到）

- [ ] **死区/载波**：示波器测 ePWM1A/1B（GPIO0/1）互补波形死区=1µs、载波=10 kHz
      （中心对齐 up-down，PRD=6000@120MHz）
- [ ] **三相互序/相差**：示波器测 A/B/C 三相高侧（GPIO0/2/4）相差 120°（电角度 5 Hz）
- [ ] **断电冷启动**：探针在线时 ROM 不交接 flash（ADR 0006），只有断电重启是
      Stand-Alone Boot 观测窗口，需人工确认 banner/自检/心跳顺序一致
