# 2026-06-20 Memory + SMP Audit Report

> 审计轮次报告。稳定 backlog 见 `../debt.md`；审计方法见 `../audit-guide.md`。

## Scope

本轮覆盖：
- D2 内存生命周期：悬垂指针、UAF、泄漏、所有权、CoW、地址空间共享。
- D3 SMP / 并发安全：全局可变状态、跨 CPU 可见性、lost-wakeup、调度共享状态。

本轮不覆盖：
- D1 架构不变量。
- D4-D12 其余维度。
- 代码修复；本轮只登记，不执行。

## Method

主要取证方向：
- 分配/释放路径：`new`、`delete`、`alloc_page`、`free_page`、`operator delete`。
- 进程退出路径：`sys_exit`、`exit_current`、`waitpid` reap。
- CoW 路径：`FLAG_COW`、`copy_page_table_level`、`handle_cow_fault`、`clear_user_mappings`。
- 共享状态：`g_registry_head`、`g_pid_alloc`、`waiting_for_child`、`quantum_remaining_`。
- 已修确认：per-CPU current、prepare-to-wait、lockdep、GS_BASE/GDT、reschedule IPI。

## Findings

登记到 `../debt.md` 的债务：

| ID | 摘要 | 优先级 |
|----|------|--------|
| DEBT-001 | `g_registry_head` 全局任务注册表无锁 | P0 |
| DEBT-002 | 退出任务的 TCB / 核栈 / 地址空间永不释放 | P1 |
| DEBT-003 | CoW 物理页无引用计数，fork+exec 可 UAF | P0 |
| DEBT-004 | `waiting_for_child` 普通 bool 跨核非原子，lost-wakeup | P0 |
| DEBT-005 | `PidAllocator` 无锁，可重复 pid | P1 |
| DEBT-006 | CLONE_VM 共享地址空间无引用计数 | P2 |
| DEBT-007 | `quantum_remaining_` 全局共享，时间片错乱 | P2 |
| DEBT-008 | signal frame 写用户栈不校验 VMA | P2 |
| DEBT-009 | `clear_user_mappings` 不识别 huge page entry | P2 |
| DEBT-010 | `FDTable` refcount 用 `guard()`，与 SMP refcount 策略不一致 | P2 |
| DEBT-011 | slab 双重释放检测为启发式 | P3 |
| DEBT-012 | execve phnum 无上限校验 | P3 |
| DEBT-013 | `sys_pipe` 写用户 int[2] 前未校验映射存在 | P3 |
| DEBT-014 | `no_reschedule_depth_` 静态全局非原子 | P3 |

## Root Cause Summary

D2 与 D3 交叉指向两个系统性洼地：

1. 进程/线程生命周期未闭环：退出释放、CoW mapcount、CLONE_VM AddressSpace refcount 是一组问题，不能孤立修。
2. F4 后仍残留未纳入 SMP 模型的全局状态：task registry、PidAllocator、waitpid wake flag。

这解释了“单核/普通测试稳定，但多进程 + 多核 + fork/exec/kill 交错后偶现挂死或 UAF”的主要风险来源。

## Non-Debt Confirmations

本轮确认以下 F4 核心项已正确处理，避免重复报告：
- `Scheduler::current()` 已 per-CPU。
- context switch 恢复点读 per-CPU current。
- Mutex/Sem/futex/waitpid 已采用 prepare-to-wait 样式关闭主要 lost-wakeup 窗口。
- `GDT::load()` 不再重载 `%fs/%gs`，避免清零 MSR base。
- lockdep 已有 per-CPU held stack、schedule-while-locked 断言、AB-BA 图。
- RoundRobin runqueue 用 `irq_guard()` 保护，`pick_next` 原子取出。
- AP idle、SharedCwd/SharedSigActions 原子 refcount、reschedule IPI 路径已落地。
- `next_tid` / `next_stack_vaddr` 已是 `lib::Atomic`。
- IRQ 路径不 sleep。

## Suggested Next Fix Track

建议开一个专项里程碑：「进程生命周期与引用计数」：

1. `DEBT-004`：去掉 `waiting_for_child` 门控，exit 无条件 `unblock(parent)`。
2. `DEBT-001` + `DEBT-005`：task registry / PidAllocator 加锁。
3. `DEBT-003`：物理页 mapcount，修 CoW UAF。
4. `DEBT-006` + `DEBT-002`：AddressSpace refcount 后接 exit cleanup。

这个顺序先降挂死风险，再处理 UAF，最后处理释放闭环，比较不容易把当前“靠泄漏维持不崩”的共享地址空间提前释放掉。
