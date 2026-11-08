# HANDOFF：iter1-batch2-threads

分支 `feat/iter1-batch2-threads`（基于 main 78c9273）。10 项按顺序做完，每项单独提交。
构建目录 `build/`（Ninja，Debug）。本机无 `/dev/kvm`，QEMU 内核测试没跑；验证靠全量编译 + host 单测 + 相关 ktest 的编译。

## 逐项改动与验证

### 1. CLONE_SYSVSEM（8d9e070）
`kernel/syscall/syscall.hpp` 加 `kCloneSysVsem = 0x20000`，`sys_clone.cpp` 的 `kServedFlags` 放行，注释写明没有 SysV 信号量实现、undo 表不共享也不复制。
验证：全量编译绿。无专项测试（clone 要真实地址空间，host 测不了）。

### 2. CLONE_FS（b9369ea）
- 新增 `kernel/fs/fs_context.hpp`：`FsContext : RefCount<>` 装 `cwd`、`root`、`umask`，`CloneFsContext` 做独立复制。
- `Task::cwd`（内嵌 char 数组）换成 `Shared<FsContext> fs_ctx`。
- fork 复制、`clone.cpp` 在 `CLONE_FS` 时共享；`sys_chdir/sys_getcwd/sys_path` 改读 `fs_ctx->cwd`。
- 新增 `test_fs_context`（3 例：默认值、复制独立、共享可见）。
验证：`test_fs_context`、`test_thread_exit`、`test_signal`、`test_scheduler`、`test_sched_class`、`test_sync`、`test_vfs`、`test_procfs`、`test_file_table` 全绿；全量编译绿。

`root` 和 `umask` 目前没有任何系统调用读写（chroot/umask 还没做），对象先带着，注释写明了。

### 3. CLONE_SIGHAND（c611cc3）
`SignalState` 改成 `RefCount<>` 派生，`Task::signals` 换成 `Shared<SignalState>`；fork 用 `CloneSignalState` 复制动作表和阻塞集、不复制 pending；`clone.cpp` 在 `CLONE_SIGHAND` 时共享。
所有 `task.signals.x` 改成 `task.signals->x`（signals.cpp、sys_signal.cpp、sys_ppoll.cpp、sys_sigtimedwait.cpp、sys_epoll.cpp、scheduler.cpp、sys_exec.cpp、相关 host 测试）。
`test_signal` 增两例：fork 副本独立、共享表一次 sigaction 两边可见。
验证：`test_signal` 33 例、`test_net_socket`、`test_syscall_face`、`test_pty_node`、`test_thread_exit`、`test_scheduler`、`test_sync` 全绿；全量编译绿。

### 4. exit_group（98b61ea）
- `sys_exit.cpp` 新增 `HandleExitGroup`，dispatch 表把 `kExitGroup` 指过去。
- `Scheduler::exit_group(code)`：用 `for_each_task` 扫同 tgid 成员。ready 成员先从就绪队列摘掉再当场退休（清 join 字、放文件表、settle 后代、离开任务表）；组长且有父进程时转僵尸并通知父进程，其余直接回收。blocked 成员只打 `group_exit_pending` 标记（它们的等待队列不由这里拆），在 `Scheduler::unblock` 或系统调用返回的 `exit_if_group_pending` 里退出。
- `Task` 加 `group_exit_pending`、`group_exit_code`。
验证：`test_thread_exit` 增两例（ready 成员被整组带走；挂起成员在别人唤醒前保持 blocked+pending，唤醒时退休），6 例全绿。

已知边界：挂在 mutex/semaphore/completion 等待队列上的成员，要等那个锁的 owner 唤醒它才真正退出；如果锁永远不被释放，成员也不会消失。这是为了不越权拆别人的等待队列，代码注释里写了。

锁序：退休成员时先清 join 字（可能进 futex 门），再进 scheduler 队列门；`unblock` 遇到待退成员时也是先放队列门再退休，避免和 futex 唤醒路径（futex 门在前、队列门在后）对撞。

### 5. futex 丢唤醒（96982b6）
`WaitOnUserWord` 不再先读用户字再进桶锁；`ParkOnWord` 在桶锁内用 `CopyFromUser` 重读并与 expected 比较。`test_scheduler` 增一例验证比较对象是当前值。
验证：`test_scheduler`、`test_thread_exit`、`test_sync`、`test_signal` 全绿；`cinux_ktest_sched` 编译通过（ktest 里三处 `ParkOnWord` 调用同步改了签名）。

### 6. WAIT_BITSET / WAKE_BITSET（1dc89ee）
- `Task` 加 `futex_bitset`，`ParkOnWord`/`WaitOnUserWord`/`WakeUserWord` 加 bitset 参数（默认全 1），唤醒条件改成地址相同且掩码相交。
- `sys_futex.cpp` 补第 9、10 号操作；val3 为零返回 EINVAL；普通 WAIT/WAKE 仍走全 1 掩码；private 位照旧剥掉。
验证：`test_scheduler` 的 guard 场景加了一次「掩码不匹配不唤醒、匹配才唤醒」；`test_syscall_face` 增一例查 EINVAL/EAGAIN/私有位/未知 op；两测试全绿。

### 7. VMA 与页表加锁（f811d21）
- `VmaStore` 加 `SpinMutex gate_`（读面也加锁），所有公开方法在 `IrqGuard` + 门内执行；内部实现拆成 `*_locked`。
- 新增 `lookup()`（锁内复制一份 `VmaRegion`，共享附件带引用计数）和 `Hold`（跨多次查询持锁，IRQ 关）。
- 新增 `reserve()`：找空位和登记在同一把锁里完成，`mmap` 非固定地址走它。
- 缺页路径 `demand::FillPage` 持 `Hold` 再 `find_locked`，填页期间 munmap 不能释放范围。
- `fault_resolve`、`cow::share_leaf`、`futex::ClearTidWord`、`sys_shm` 的 Shmdt、`vma_diag` 改读副本，消掉返回指针逃逸后的释放后使用。
- `AddressSpace` 加 `gate_`，`init/map/map_user/translate/teardown` 自带锁；暴露 `guard()` 给 munmap、mprotect 的直接走表。锁序：VMA 门在前，页表门在后。
- `sys_mmap` 固定地址分支仍先 reclaim 再 register。
验证：`test_vma` 29 例（新增副本存活、reserve 连续分配两例）、`test_syscall_face`、`test_thread_exit`、`test_scheduler`、`test_fs_context` 全绿；`cinux_ktest_{aspace,cow,demand,vma}` 与全量编译通过。

未覆盖：`cow::CloneSharedSpace` 走父页表时没持父世界页表门（fork 与父进程其他线程 munmap 并发时仍有窗口），已作为风险记下，没有硬改。

### 8. 缺页已 present 重试（1de064e）
`FillPage` 返回值改成 `FillOutcome { kUnhandled, kFilled, kRetry }`；已 present 返回 `kRetry`；`fault_resolve` 把 `kFilled`/`kRetry` 都当已解决，指令重试，不再误发 SIGSEGV。
验证：全量编译；`cinux_ktest_{demand,loader,cow,vma}` 编译通过（demand/loader 的断言已按三态改）。

### 9. TLB shootdown（6681c96）
按任务允许的降级路线只写设计笔记：`document/notes/2026-11-08-tlb-shootdown-design.md`。写了现状（本地 `invlpg`、现有 0xE0 reschedule IPI）、为什么单核测不出来、方案 A 全量 CR3 重载广播、方案 B 按地址定向 shootdown、锁序/中断/PCID 约束、验证计划和要改的文件。`InvalidatePage` 的注释补了「只清本地」并指向笔记。
验证：只改注释和文档，全量编译绿。

### 10. 锁与引用计数 RAII（2f3ad48）
- `SpinGuard`/`MutexGuard` 从手写类改成 `base::ScopedGuard` 的策略别名（`SpinLockPolicy`/`MutexLockPolicy`），锁进出只剩 lock/unlock 两处，和 `base::SpinMutex::Guard` 用同一套壳。
- `shared.hpp` 加 `AdoptShared(object)`：retain + 构造 Shared 收成一处；`vfs.cpp` 挂载生命周期改用它，不再手写 retain。
验证：`test_sync`、`test_guard`、`test_shared`（新增 AdoptShared 一例）、`test_ref_count`、`test_lockdep`、`test_scheduler`、`test_vfs`、`test_signal`、`test_thread_exit`、`test_fs_context` 全绿。

## 收尾验证

- `cmake --build build -j6`：绿（未跑 QEMU 内核测试，本机没有 /dev/kvm）。
- 受影响 host 测试逐个构建并运行：见上，全绿。
- 相关 ktest 只做了编译：`cinux_ktest_{sched,aspace,cow,demand,loader,vma}`。

## 剩什么 / 风险点

1. TLB shootdown 没实现。SMP + CLONE_VM + 并发 unmap/protect 下仍有 stale TLB 的静默写坏风险，笔记里写了完整方案。
2. exit_group 对挂在锁等待队列上的成员只打标记，退出时间取决于锁的释放；锁永不释放则成员不消失。
3. `cow::CloneSharedSpace` 没持父世界页表门；fork 与父进程其他线程 munmap 并发时可能克隆到一半的页表。
4. VMA 门的临界区里有堆分配（`new VmaRegion`、共享附件构造）；当前堆不睡眠，与「锁内不许 schedule」不冲突，但以后堆若可睡眠要重新评估。
5. `FillPage` 在 `kRetry` 时无条件让指令重跑：如果某天出现「VMA 说可执行、叶项却是 NX」这种内核自相矛盾，会变成无限重试而不是 SIGSEGV。
6. `futex_bitset` 挂在 Task 上，同一任务连续两次 park 会覆盖，但 park 期间任务不可能再 park，语义没问题。
7. exec 在共享信号表的线程组里调用 `signals->reset()` 会把整组表一起清掉；没有 de_thread 之前这是“整组重置”而不是 Linux 的“先拆组再重置”。单线程 exec 不受影响。
8. 剩余 retain/release 手写点（ramfs、pipe、socket、eventfd、epoll、input_node）都是 FileOps 的 open/release 回调配对，不是作用域生命周期，RAII 化需要改 VFS 协议，这次没动。

## 对抗验证后的修复（2026-11-08）

验证 agent 报了三处，前两处是真缺陷，已修（commit `566c45c`）：

- 唤醒自锁：`WakeUserWord` 原先持桶锁调 `unblock`，组员退休时 `ClearTidWord` 再进 futex 门，非递归锁同核自旋。改成锁内摘链、锁外 `unblock`，每批 16 个。
- 组员提前释放：`retire_group_member` 原先在判断 running 之前就 `files.reset()`，别的 CPU 上的组员会拿到空表。现在先持队列门确认状态，running 只打 pending，锁外再清 files 与 join 字。
- exit_group 裸指针：先持门认领并摘表，副作用在锁外；victim 不再跨锁使用，避免另一 CPU 回收后的释放后使用。
- futex 锁内读：锁外先 touch 页，锁内确认 VMA 仍在，首触页的 `FillPage` 不会在自旋里跑。表项已 present 的 spurious fault 走重试，不再 SIGSEGV。

验证：`test_thread_exit` 6/6、`test_scheduler` 7/7、`test_sync` 9/9、`test_signal` 33/33、`test_net_socket` 25/25、`test_syscall_face` 23/23、`test_fs_context` 3/3、`test_vfs` 15/15、`test_procfs` 18/18、`test_sched_class` 9/9，全量编译绿。

仍留：futex 锁内读依赖「VMA 在则页在」（Cinux 没有 madvise），munmap 后同址重映射的竞赛窗口记在这里；TLB shootdown 仍未实现。
