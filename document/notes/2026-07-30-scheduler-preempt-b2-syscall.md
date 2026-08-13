# 调度器任意内核态抢占 B2：syscall 内核段

日期：2026-07-30

## 背景

B1 已能在硬中断的 post-EOI 公共尾抢占普通内核任务，但 x86-64 `SYSCALL`
入口仍由 SFMASK 全程保持 IF=0。于是系统调用里的长计算路径只能靠显式
`yield()` 或阻塞切换，尚不满足“任意非临界内核代码可被时钟抢占”。

过去文档把 `sti-in-syscall` 直接等同于 `#DF`。那是旧实现的症状，不是永久
架构约束：当时 IRQ 返回帧会挂在共享栈上，`context_switch` 又无条件 `sti`，
切走后下一次 IRQ 可覆盖尚未恢复的 syscall continuation。B0 的任务私有 IRQ
栈与 IF 上下文、B1 的 post-EOI 单一切换点已经消除了这两个根因。

## 实现

- `syscall_entry` 仍依赖 SFMASK 以 IF=0 进入；完成全部用户 GPR、SYSRET 状态和
  FXSAVE frame 后，改调 `syscall_dispatch_preemptible`。
- 该汇编 wrapper 执行 `sti`，转发栈上的第七个 SysV C 参数并调用
  `syscall_dispatch`；返回后第一时间 `cli`，再回到不可抢占的 frame restore /
  `swapgs` / `sysretq` 尾。
- wrapper 入口满足 `rsp % 16 == 8`。`pushq 8(%rsp)` 同时复制第七参数并把嵌套
  `call` 前的栈对齐到 16 字节；返回后只弹这一个转发槽，不改变外层 frame。
- syscall、IRQ 与 exception 入口统一执行 `cld; clac`。用户或被中断的
  uaccess 不能把 DF/AC 泄漏给内核 C/C++；硬件 IRQ frame 保存原 RFLAGS，
  `iretq` 会在原任务恢复时精确还原 AC/DF/IF。
- `copy_to_user` / `copy_from_user` 仍不得主动阻塞或 `yield()`。允许硬 IRQ
  抢占其 AC=1 窗口，是因为 IRQ 入口先 CLAC，切走后的任务看到 AC=0；原访问器
  最终通过 IRET 恢复 AC=1 后继续并自行 CLAC。
- 清理生产注释中“syscall 内 STI 必然损坏 frame”的旧结论。驱动与 wait queue
  仍不得在 ISR handler 内直接 `schedule()`；切换权继续只属于 EOI 后的公共
  `irq_exit`。

## 机制回归

`test_long_syscall_is_timer_preempted` 调用生产使用的同一个汇编 wrapper，并注册
一个不含 `yield()` 的长 syscall handler：

1. handler 观察 IF=1，主动设置 AC=1、DF=1 后纯忙等；
2. 真实 PIT timer IRQ 抢占它，并从 IRQ-exit 切到同优先级 peer；
3. peer 观察 IF=1、AC=0、DF=0，证明中断入口隔离了被打断的 uaccess/方向标志；
4. 切回 handler 后观察 AC=1、DF=1，证明 IRET 恢复了原 continuation；
5. wrapper 返回时观察 IF=0，并核对 syscall 返回值。

第一版机制测实际抓到过一个入口错误：`clac` 放在条件 `swapgs` 的跳转之前，
内核态 IRQ 会跳过它，peer 因而看到 AC 仍为 1。把 `cld; clac` 移到合流 label
之后，确保用户态与内核态来源都执行，再通过上述回归。

压力复跑还暴露了机制测自身的生命周期问题：测试曾把栈上的 `RoundRobin` 注册到
全局调度器，函数返回后留下悬空策略指针，后续测试走到空 runqueue 时会产生未定义
行为。B1/B2 的测试策略均改为静态生命周期，并在注册前清空队列，消除跨 suite
污染。

## 验证

- `cmake --build build --target run-kernel-test-all -j$(nproc)`：
  单核与 SMP 两 leg 均为 939 passed，0 failed；
- 额外连续三轮 `run-kernel-test-smp` 均为 939 passed，0 failed，AP
  wake/readback 与 TLB shootdown IPI 均 PASS；
- 开启 `CINUX_MUSL_HELLO_SMOKE` 的真实 ring3 `/hello` fork/exec/write/exit
  smoke 为 20/20 PASS。

B2 之后，普通内核任务、硬 IRQ 打断的内核 continuation 和 syscall dispatcher
都具备时钟抢占能力；Spinlock/InterruptGuard 的 `preempt_count` 仍定义不可抢占
临界区。
