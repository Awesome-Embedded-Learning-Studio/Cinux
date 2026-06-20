# F-QA Q1-3~5 — CI 强化（matrix + 测试项数门禁 + host ASAN 基建）

> 里程碑：F-QA Q1（零成本门禁，批 3-5）。日期 2026-06-20。分支 `feat/f-qa`。

## Q1-3 CI kernel-tests 多 config 矩阵

`ci.yml` 的 kernel-tests 从单 Release 扩成 `{Debug,Release}×{none,ubsan,lockdep}` = 6 组合并行。UBSAN/LOCKDEP 从"本地 opt-in"升级为"CI 常驻回归"；Debug(-O0) vs Release(-O2) 双守（FO GOTCHA：CMAKE_BUILD_TYPE 默认空首次 -O2）。artifact name 按 matrix 区分。yaml 语法验证 OK（实际 6 组合 CI push 后验证）。

## Q1-4 测试项数单调不降门禁

`scripts/check_test_count.sh`：解析 run-kernel-test 的 `Tests: N passed, M failed`，断言 `N >= baseline(875)`。防 silent test loss（RUN_TEST 被删 / 整模块测试被跳过却读"绿"）。每 matrix config 跑。本地三 case 测过：OK(875≥875) rc=0、regression(800<875) rc=1、failures(3 failed) rc=1。

## Q1-5 host ASAN/UBSAN/gcov 基建 + 发现 ring_buffer OOB

`test/CMakeLists` 加 `CINUX_HOST_ASAN` option（ON 时 host test 加 `-fsanitize=address,undefined --coverage`，零内核改动，host-only）。**首次本地跑（`make test_host` ASAN）立刻抓到真 bug**：

- `third_party/Cinux-Base/include/cinux/ring_buffer.hpp:73` `RingBuffer<char,4096>::push_batch` **global-buffer-overflow**（Cinux-Base 子模块，F1-M1 迁移的 RingBuffer，production pipe/keyboard 在用）；
- 2 处内存泄漏（24104B/1 alloc + 18624B/776 alloc）；
- 3 个 host test（multi_terminal/fd_table/pipe）失败。

单核严格串行没踩关键数据故潜伏，ASAN 一开即抓。登记 **DEBT-017**（P1：碰子模块修 `push_batch` 边界 + 泄漏 cleanup）。

因 DEBT-017 未修，`ci.yml` host-tests **暂不 flip `-DCINUX_HOST_ASAN=ON`**（注释已标，修后启用），避免 break CI。`CINUX_HOST_ASAN` 基建就位（本地可用 + 首跑立功）。TSan 与 ASan 互斥，留 follow-up（单独 `-DCINUX_HOST_TSAN` build）。

## 验证

- Q1-3：ci.yml yaml 语法 OK（matrix 字段确认）。
- Q1-4：`check_test_count.sh` 本地三 case 测过。
- Q1-5：`CINUX_HOST_ASAN` 本地跑通并抓到 DEBT-017；ci.yml 不启用故不 break CI。

## 产出

- `.github/workflows/ci.yml`：kernel-tests matrix（Q1-3）+ check step（Q1-4）+ host Configure 注释指 DEBT-017（Q1-5）。
- `scripts/check_test_count.sh`（Q1-4，可执行）。
- `test/CMakeLists.txt`：`CINUX_HOST_ASAN` option（Q1-5）。
- `document/todo/quality/debt.md`：DEBT-017（ring_buffer OOB + 泄漏）。
- 后续：DEBT-017 修（push_batch 边界 + 泄漏）→ ci.yml host ASAN 启用；Q2 deterministic 审计方法论；Q3 系统审计；Q4 类型不变量 + 修债。
