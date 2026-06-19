# F-INFRA I-10 — lockdep Part1（持锁跨 schedule 检测，opt-in）

> 日期 2026-06-19 · F-INFRA Tier3 批 I-10 · 分支 `feat/finfra`

## 背景
R6 Part1：零锁序/持锁校验。单核下持 spinlock 跨 `schedule()` 必死锁（被调度的任务释放不了调用方还持有的锁，调用方再也不运行）。F4 SMP 前把这类 bug 变成**显式 panic**而非静默挂死。R6 Part2（锁序图 DFS）划 F4-M5（单核下 ABBA 不可能发生）。

## 目标
opt-in：`schedule()` 入口断言「无 spinlock 被持有」，违反即 kpanic + backtrace。

## 设计/决策
- **`CINUX_LOCKDEP` CMake 选项（默认 OFF）**：零成本 opt-in。`cmake -DCINUX_LOCKDEP=ON`。OFF 时全部 `#ifdef` 编译排除，默认构建/CI 完全无感。
- **`g_lockdep_held_depth` 全局计数**（`kernel/proc/sync.cpp`，命名空间 `cinux::proc`）：`Spinlock::acquire()` 成功获锁后 `++`、`release()` 清锁前 `--`。Guard/IrqGuard 都走 acquire/release，故全覆盖。
- **`schedule()` 入口断言**（`current_!=nullptr` 检查之后）：`if (held_depth>0) kpanic(...)`。放入口而非体内：`pick_next`/`dequeue` 内部会短暂获+释 RoundRobin 锁，入口断言只查**调用方**遗留的锁（真正要抓的）。
- **不计数 `block()`**：block() 先 `dequeue`（获+释锁）再 `schedule()`，到 schedule 入口锁已释放 → 不误报。
- **重入**：kpanic noreturn，dump_memory_stats 期间再获锁（held_depth++）不再被查，无需 in_panic 标志（验证器建议的，实测本断言不需要）。

## 验证（双构建）
- **默认构建（LOCKDEP OFF）**：`timeout 40 ... run-kernel-test` → **840/0**（lockdep 编译排除，行为零变化）。
- **lockdep 构建（ON，独立 build-lockdep）**：`cmake -DCINUX_LOCKDEP=ON -DCINUX_BUILD_TESTS=ON` → `run-kernel-test` → **840/0、无 lockdep panic**。证明：① 仪器编译运行正常；② inc/dec 平衡（每 acquire 有对应 release）；③ 内核测试套从不在持锁时 schedule（无误报）。F4 SMP bring-up 时若引入持锁跨调度，此断言会即时 panic 暴露。
- 主动触发冒烟（故意持锁+schedule）留人工：构造它需真实调度器状态、且就是它要抓的 bug 本身；无 false-positive 运行 + 平衡的 inc/dec + 平凡断言条件已足够置信。

## 陷阱
- **clang-tidy（I-8）报 `scheduler.cpp:152` `prev->addr_space` 可能 null 解引用**——经复核是**假阳性**：`prev = current_`，schedule 入口已查 `current_!=nullptr`，analyzer 跨函数边界没追到这个不变量。属 triage 结论（I-8 finding → 假阳性，无需改）。

## 文件
- 改：`CMakeLists.txt`（CINUX_LOCKDEP 选项）、`kernel/CMakeLists.txt`（compile_definitions）、`kernel/proc/sync.{hpp,cpp}`（held_depth + inc/dec）、`kernel/proc/scheduler.cpp`（schedule 入口断言）。
