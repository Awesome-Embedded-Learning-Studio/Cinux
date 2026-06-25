# F-CLN 批7 — DEBT-007 quantum per-task — 2026-06-25

> DEBT-007（quantum_remaining_ 单一共享 quantum → 多核时间片错乱）闭环。中风险（调度核心）。

## 问题
RoundRobin 类单一成员 `quantum_remaining_`（scheduler.hpp），两核 tick 各自递减同一变量：
- 实际时间片变 `DEFAULT_TIME_SLICE / ncpus`（2 核 → 1 tick）
- 一核 recharge（quantum=0 时重置）重置另一核正在跑的任务
行为错（非崩溃），调度不可预测。

## 修复（per-task，对齐 Linux）
quantum 从 RoundRobin 类成员 → **`Task::quantum_remaining`**（per-task 字段，对齐 `task_struct->rt.time_slice`）：
- **process.hpp**：Task 加 `int32_t quantum_remaining`
- **scheduler.hpp**：RoundRobin 删 `quantum_remaining_` 成员
- **roundrobin.cpp**：
  - ctor：删 `quantum_remaining_` 初始化
  - pick_next：`task->quantum_remaining = DEFAULT_TIME_SLICE`（per-task 重置）
  - clear：删 `quantum_remaining_` 重置
  - task_tick：递减 `current->quantum_remaining`（per-task），==0 抢占
  - task_fork：`child->quantum_remaining = DEFAULT_TIME_SLICE`（覆盖 memcpy 拷的 parent 值）
- **task_builder.cpp**：build() 设 `task->quantum_remaining = DEFAULT_TIME_SLICE`（新任务满量子）

## 验证
- big_kernel 编译零 error/warning
- run-kernel-test **931/0**（单核 per-task 等价旧共享，回归）
- **-smp 2** ALL PASSED（多核 per-task，DEBT-007 SMP 回归）
- host ctest **54/0**（Task 字段加，host mock Task 回归）

## 关联
- F3-M4 把 quantum 从 Scheduler::current_slice_ 移入 RoundRobin 类（内聚），但仍是单一共享；本批改成 per-task 真正消多核竞争
- GOTCHA#23 同族（context_switch 恢复点读 per-CPU current）—— 调度核心改动 -smp 2 回归必做

## 产出
- process.hpp（Task 字段）+ scheduler.hpp（删成员）+ roundrobin.cpp（5 处）+ task_builder.cpp（build 初始化）+ debt.md DEBT-007 ✅ + PLAN 批7 ✅

下个：批8 收尾（ROADMAP/PLAN/debt/notes + 验证矩阵）。
