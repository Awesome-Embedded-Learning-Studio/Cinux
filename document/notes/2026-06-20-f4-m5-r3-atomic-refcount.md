# F4-M5 Batch 1 — R3 原子引用计数(SharedCwd / SharedSigActions)— 2026-06-20

> F4-M5(同步原语)Batch 1。F4-M4 让 AP 真跑线程后,F3-M2「共享 refcount 指针化」对象从单核假装安全变成真跨核并发——非原子 `++refcount`/`--refcount` 会丢更新 → use-after-free / leak。本批把两个无锁保护的共享 refcount 对象原子化。分支 `feat/f4-m4-2-3-migration`(叠在 M4-2-3 上,前置未 push,见工作流偏好)。

## 范围修订(执行时核对)

plan 原列 3 个对象(SharedCwd / SharedSigActions / FDTable),执行核对发现 **FDTable 已安全**:

- **FDTable**([file.hpp:156](../../kernel/fs/file.hpp#L156) `mutable Spinlock lock_`):`acquire()`/`release()` 已在 `lock_.guard()` 内(file.cpp:30/38)→ 自旋锁跨核串行化 ++/-- → refcount 已 SMP 安全。且 `release()` 的 `close()` 在锁作用域外(`{}` 块后)调,无自锁死锁。**跳过 FDTable**(不 churn 已正确代码)。

故 R3 实际范围 = **SharedCwd + SharedSigActions**(两者 raw `++`/`--`,无锁)。

## 改动

两个对象同款非原子模式 → `__atomic` 内建(免改字段类型、免改 `lib::Atomic`——它有 `fetch_add` 无 `fetch_sub`):

```cpp
void acquire() { __atomic_add_fetch(&refcount, 1, __ATOMIC_ACQ_REL); }
void release() {
    if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_ACQ_REL) == 0) {
        delete this;
    }
}
```

- **[shared_cwd.hpp](../../kernel/proc/shared_cwd.hpp)** `SharedCwd::acquire/release`(CLONE_FS 线程共享 cwd)。
- **[signal.hpp](../../kernel/proc/signal.hpp)** `SharedSigActions::acquire/release`(CLONE_SIGHAND 线程共享信号表)。

**ACQ_REL 选用**:add 侧 release 让别核看到对象已建好;**sub 到 0 那次 release** 让 deleter 看到对象全部状态(安全 `delete this`)。canonical 原子 refcount 模式。
**去掉旧 `refcount > 0` 预检查**:它是 racy 守卫(load→check→sub 之间有别核 sub),非真保护;canonical 原子 refcount 不预检查(double-release 是应被别处抓的逻辑 bug,原子版同样无法救)。

## 为何现在才暴露

F3-M2 引入这些共享 refcount 对象时是单核(无真并发,raw ++/-- 「靠时序侥幸」)。F4-M4 M4-2-3 让 AP 真跑 user task/线程(-smp 2 双核实跑 CLONE_FS/SIGHAND/FILES)后,两核同时 clone/fork/exit 触发 acquire/release 才真丢更新。R3 是 F4-M4 实多核后的必然加固(F-INFRA 划归 F4-M5)。

## 验证

- **单核 run-kernel-test:875 passed, 0 failed**(原子 op 单核等价正确)。
- **全量 `cmake --build build`**(改公共头 shared_cwd.hpp/signal.hpp,CI 盲区)零警告通过。
- **-smp 2 `run-smp` 冒烟**:0 fault,全启动到 Milestone 035 + GUI Desktop + AP1 online + kernel_init 正常 exit(原子化不破坏多核启动)。
- clang-format 过。

## 不做(本批 / 后续批)

- **FDTable**:已 lock 保护,跳过(范围修订)。
- **Batch 2 waitpid SMP 安全**:children 链表 lock-free,加 per-parent 锁 + double-check(F4-M5 下批)。
- **Batch 3 R6-Part2 lockdep 锁序图**:per-CPU 持锁栈 + 锁序图 DFS(opt-in CINUX_LOCKDEP,较大,可延后)。

## 关键文件

- [kernel/proc/shared_cwd.hpp](../../kernel/proc/shared_cwd.hpp)、[kernel/proc/signal.hpp](../../kernel/proc/signal.hpp)。

## 参考

- 计划:`.claude/plans/temporal-jumping-hartmanis.md`(F4-M5 全范围)。
- F3-M2 引入共享 refcount:`document/notes/2026-06-18-f3-m2-*.md`。
- F4-M4 M4-2-3 让 AP 真跑线程:`document/notes/2026-06-20-f4-m4-2-3-migration.md`。
