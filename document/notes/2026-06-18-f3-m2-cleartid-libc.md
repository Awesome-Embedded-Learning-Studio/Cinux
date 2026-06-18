# F3-M2 批5 — cleartid 集成 + libc wrapper + 收尾

> 2026-06-18。F3-M2 批5（收尾）：CLONE_CHILD_CLEARTID exit 集成（连 clone+futex+exit）+ libc clone/futex wrapper + cleartid 单测。1 批，809→810（+1 测试），fresh 810/0。**真用户态线程 round-trip + 实机 GUI 冒烟留 follow-up**（需用户线程程序 + libc 跑通）。

## 范围

- **CLONE_CHILD_CLEARTID exit 集成**：`task_exit_cleartid(Task*)`——若 `clear_child_tid != 0`，写 0 到该用户地址 + `futex_wake_addr(addr, 1)` 唤醒一个 waiter（pthread_join 协议：joiner futex_wait 在 ctid，线程 exit 时内核清零 + 唤醒）。sys_exit 在 SIGCHLD/state=Dead 之前调用。
- **futex_wake_addr 暴露**：sys_futex 把内部 `futex_wake` 经 `futex_wake_addr(uaddr, max)`（全 bitset）暴露给 cleartid 的退出路径用（无 syscall 边界）。
- **libc wrapper**：`user/libc/syscall.{h,cpp}` 加 `sys_clone`/`sys_futex` + `_syscall5`/`_syscall6`（r10/r8/r9 寄存器约束）+ CLONE_*/FUTEX_* 用户常量。musl pthread 的内核契约就绪。
- **cleartid 单测**：waiter futex_wait 在 ctid（phantom-task 模式 block 返回）→ task_exit_cleartid → 验证 ctid 置 0 + waiter Ready；clear_child_tid==0 时 no-op。

## 关键点

- **cleartid 连起 clone+futex+exit**：clone(CLONE_CHILD_CLEARTID) 记 `clear_child_tid=ctid`（批4）→ 线程跑 → sys_exit → task_exit_cleartid 写 0 + futex_wake → joiner 醒。三件套闭环（F3-M2 的集成价值）。
- **libc _syscall5/6**：x86-64 syscall 第 4/5/6 参走 r10/r8/r9（非 rcx，rcx 是 rip）。用 `register uint64_t rN __asm__("rNN")` 约束保证编译器分配正确寄存器。

## 验证

- 单测 +1：cleartid 置 0 + 唤醒 + no-op。
- 全量：fresh `run-kernel-test`（`timeout 40`）809→**810/0**，ALL TESTS PASSED。
- libc 编译过（user_binary_obj）。

## F3-M2 收官 + Follow-up

**F3-M2 完成（5 批，783→810，+27 测试）**：TLS(fs_base) + futex + 共享 refcount + clone 核心 + cleartid/libc。线程支持内核侧地基就绪。

**Follow-up（统一收尾，非本里程碑门）**：
- **真用户态线程 round-trip**：需用户线程程序（musl pthread 或手写 clone+futex）跑通 clone→线程执行→futex 同步→join。当前 run-kernel-test 不跑调度循环，测不到真线程；实机需 libc + 用户程序。
- **实机 GUI 冒烟**：`make run` 非 make 目标，`cmake --target run` 重建 mini_kernel 慢 + headless GUI。clone 改动高危（启动路径），真机跑通才算闭环。
- **AddressSpace refcount**：clone(CLONE_VM) 共享 addr_space 指针但**无 refcount**（线程 exit 不释放，进程 exit 才 leak）——同 fd_table 批3 前的 latent。需补 refcount 才能安全释放。
- **futex timeout**：需 PIT timer 定时唤醒（wait_queue + tick 比较）。
- **getpid/gettid 区分**：当前 getpid 返 tgid（线程同），无 gettid（线程自身 id）。
- **线程 exit 不发 SIGCHLD**（仅进程 exit 发）：当前 sys_exit 总发，线程 exit 不该发（minor POSIX 偏差）。
