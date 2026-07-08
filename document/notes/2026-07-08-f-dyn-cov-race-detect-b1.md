# F-DYN-COV 批1 — SMP data-race 检测基建

**日期**：2026-07-08
**分支**：feat/boost_cinux（批1，未 push）
**前序**：B3 mm 三线 SMP race 收尾（mm buddy `c255953` / ext2 block_buf_ `9eabf0d` / NVMe io_submit `35a981c`，同模式「共享可变状态无锁」）。audit 发现一大片未修（ext2 inode_cache_/位图/sb/bgdt、AHCI、e1000、xhci）。靠人 audit 慢且易漏 → 建基建。

## Context

用户要「试看 SMP race 能不能抓出来」，用 ext2 inode_cache_（全程无锁）当验证靶子。工具能抓到它就有效。

**关键发现**：LOCKDEP 已有 per-CPU 持有栈（[lockdep.cpp:27-34](kernel/proc/lockdep.cpp#L27-L34) `g_held[cpu_id]`，Spinlock::acquire/release 已调 `lockdep_acquired/releasing`），但缺 `lockdep_is_held` / `lockdep_assert_held`。基建大半就绪，扩展即可，不从零造 KCSAN。

## 设计：两个互补工具

**工具 A — lockdep_assert_held**（扩 LOCKDEP，抓「有锁忘持」= 防已修 race 回归）
- lockdep.hpp/cpp 加 `lockdep_is_held(lock)`（遍历 `g_held[cpu].held[]`，O(depth)）+ `lockdep_assert_held` 宏
- 局限：抓不了「根本没锁」的 inode_cache_（没锁可 assert）

**工具 B — RaceWatchpoint + RACE_TOUCH**（新基建，抓「根本没锁」）
- `RaceWatchpoint { name; last_cpu }`，`RACE_TOUCH(w)` 记录 last_cpu，跨 CPU 交错 → `kpanic("[SMP-RACE]")`
- `race_check_access_probe(w)` 返 bool 不 panic（机制测试用，验证检测逻辑不挂内核）
- `CINUX_RACE_DETECT` option 默认 OFF（opt-in debug，atomic exchange 开销 + 改 timing 有 heisenbug 风险，不适合常驻）

## 实现

- [lockdep.{hpp,cpp}](kernel/proc/lockdep.cpp)：加 `lockdep_is_held`（遍历 held 栈，照 `lockdep_held_depth` 模式）
- [race_detect.{hpp,cpp}](kernel/proc/race_detect.cpp)（NEW）：`RaceWatchpoint` + `race_check_access[_probe]` + `RACE_TOUCH`/`lockdep_assert_held` 宏（CINUX_LOCKDEP/CINUX_RACE_DETECT off 时 no-op）
- [race_detect_stub.cpp](kernel/proc/race_detect_stub.cpp)（NEW）：OFF stub（probe 返 false、access 空）
- [cmake/options.cmake](cmake/options.cmake)：`option(CINUX_RACE_DETECT ... OFF)` + 加 `RACE_DETECT` 到 `CINUX_COMPILE_DEF_OPTS`
- [kernel/proc/CMakeLists.txt](kernel/proc/CMakeLists.txt)：§14 file gate（race_detect.cpp vs stub）
- [main_test.cpp](kernel/test/main_test.cpp)：机制测试合并 `run_smp_ap_wake_test`——AP selfcheck 调 probe 设 last_cpu=AP，BSP 调 probe 验证返 true（`#ifdef CINUX_RACE_DETECT` 包，OFF 跳过=零行为改变）

## 验证

- build-verify KVM=ON -smp2 leg：`[F-DYN-COV] race-detect test: PASS (detected cross-CPU)` + ALL TESTS PASSED
- build-verify OFF 单核 leg：ALL TESTS PASSED（机制测试 `#ifdef` 跳过，零行为改变）
- GOTCHA：第一次 run-kernel-test-all -smp2 卡在 shootdown IPI test（KVM -smp2 IPI 偶发 heisenbug，重跑过；非本批引入，memory `-smp` 偶发已知）
- GOTCHA：宏不能加命名空间前缀（`cinux::proc::RACE_WATCHPOINT_INIT` 错 → `RACE_WATCHPOINT_INIT`，宏非函数）
- GOTCHA：GUI QEMU 持 ahci_test.img 写锁 + 占 -vnc :0 → console gate 改 build-verify + `sed -vnc :5` 跑完 `git checkout`（memory verify-vnc-port-collision）

## 后续

- **批2**：ext2_inode.cpp `get_cached_inode` 加 `RACE_TOUCH`，用户启 GUI -smp2 跑抓 `[SMP-RACE]`（不修 race，只验证工具有效）
- **批3**：ext2 加 `inode_cache_lock_` Spinlock + `lockdep_assert_held`，闭环阻塞 gcc 的崩
- 未集成 race（ext2 位图/sb/bgdt、AHCI、e1000、xhci）：用同一基建逐个标 `RACE_TOUCH` → 抓 → 修
