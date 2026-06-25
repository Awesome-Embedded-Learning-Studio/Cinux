# F-CLN 批6 — DEBT-010 FDTable refcount 对齐 R3 — 2026-06-25

> DEBT-010（FDTable refcount 用 guard() 非 atomic，与 R3 不一致）闭环。

## 问题
FDTable::acquire/release（[file.cpp:43-68](../../kernel/fs/file.cpp#L43)）用 `lock_.guard()` + `++refcount_`/`--refcount_`（非原子），对照 SharedCwd/SharedSigActions（F4-M5 R3）已改 `__atomic_*_fetch(ACQ_REL)`。CLONE_FILES 线程跨核共享 FDTable，多核真并发 acquire/release 时 refcount_ 竞争（lost update）。

## 修复（对齐 R3）
acquire/release 改 atomic：
```c++
acquire()  { __atomic_add_fetch(&refcount_, 1, __ATOMIC_ACQ_REL); }
release()  { if (__atomic_sub_fetch(&refcount_, 1, __ATOMIC_ACQ_REL) == 0)
                 { close all + delete this; } }
```
- 去 `lock_.guard()`（refcount 无锁）
- 去 racy `refcount_ > 0` 守卫（正确生命周期不 underflow，R3 风格）
- release 到 0 独占（无其他引用），读 fds_[] 无竞争；close() 持锁
- ACQ_REL：release-to-0 须见此前所有写（pair acquire）
- **alloc/close/get/set 保留 `lock_.guard()`**（fds_[] 数组保护，非 refcount；当前 IRQ 不触达 FDTable，未来触达再升 irq_guard）

## 验证
- big_kernel 编译零 error/warning
- run-kernel-test **931/0**
- host ctest **fd_table/pipe/shell_redirect/shell_write/sys_pipe 全过**（FDTable 改动）
- **-smp 2** ALL PASSED（SMP refcount 回归）

## 产出
- file.cpp（acquire/release atomic）+ debt.md DEBT-010 ✅ + PLAN 批6 ✅

下个：批7 DEBT-007 quantum_remaining_ 改 per-task（中风险，调度核心）。
