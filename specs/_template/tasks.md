# tasks.md — <特性名称>

> 状态：`draft` ｜ 上游：`requirements.md` / `design.md`

## 任务清单

- [ ] **T-001**（REQ-001）实现 `<module>` 纯逻辑模块
      - 文件：`projects/<proj>/source/<module>.c`、`source/<module>.h`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\<proj>`
- [ ] **T-002**（REQ-001）编写主机侧单测覆盖全部 EARS 验收句
      - 文件：`harness/tests/host/test/test_<module>.c`
      - 验证：`.\harness\tools\run-host-tests.ps1`
- [ ] **T-003**（REQ-002）实现硬件层并接入 main
      - 文件：`projects/<proj>/source/<module>_hw.c`、`source/main.c`
      - 验证：`.\harness\tools\ccs.ps1 build -Project projects\<proj>`
- [ ] **T-004**（REQ-00X）更新工程 README 与 target card
      - 验证：人工复核
- [ ] **T-005**（REQ-00X）填写 `trace.json` 并跑 `.\harness\tools\spec.ps1 check`
      - 验证：`.\harness\tools\spec.ps1 check -Id <spec-id>`

## 备注

- <实现说明/注意事项>
