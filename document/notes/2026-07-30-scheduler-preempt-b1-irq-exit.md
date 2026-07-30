# 调度器任意内核态抢占 B1：IRQ-exit 切换

日期：2026-07-30

## 背景

B0 已让 `context_switch` 保存并恢复 IF，并为每个任务建立独立、带 guard page
的 IST2 IRQ 栈。B1 在这个安全底座上把 `need_resched` 从仅记录意图的标志变成
真实抢占请求。

调度不能直接发生在 PIT/LAPIC handler 内：控制器尚未 EOI，且 handler 仍在使用
当前中断上下文。统一切换点放在 controller EOI 之后、寄存器与 FXSAVE 区尚未弹出
之前。此时挂起任务的完整 IRQ continuation 留在它自己的 IRQ 栈上；任务再次被调度
时从同一 continuation 恢复，最终正常 `iretq`。

## 实现

- PIC/APIC 的 `ISR_IRQ` 公共尾在 `irq_eoi_isr` 后调用 `irq_exit()`。
- PIT、LAPIC timer、网络软件 timer 属于 handler 自行 EOI 的入口，因此在各自
  EOI 后调用同一个 hook。
- timer quantum、unblock 与 reschedule IPI 仅以原子 release store 发布
  `PerCpu::need_resched`。
- `Scheduler::preempt_from_irq()` 只在调度器已初始化、有当前任务、
  `no_reschedule_depth==0` 且 `preempt_count==0` 时，以原子 exchange 消费请求并
  调用 `schedule()`。
- spinlock 与中断 guard 持有期间请求保持 pending，不会在不安全区切换；后续 IRQ
  退出会再次检查。
- reschedule IPI 不再依赖 AP 回到 idle loop；它在 LAPIC EOI 后直接走统一
  IRQ-exit 切换点。

## 机制回归

`test_timer_preempts_kernel_task` 使用真实 PIT IRQ 和两个同优先级内核任务：

1. worker 先持有 `Spinlock` 跨过四次 PIT tick，确认 peer 没有运行；
2. worker 解锁后继续纯忙循环，不调用 `yield()`、阻塞或退出；
3. quantum 到期后，timer IRQ 的退出 hook 切到 peer；
4. peer 运行并返回，worker 后续恢复此前挂起的 IRQ continuation。

这同时证明了“临界区延迟抢占”和“普通内核代码可被 timer 强制抢占”，而不是通过
合作式 `yield()` 伪造切换。

## 验证

- `cmake --build build --target run-kernel-test-all -j$(nproc)`：
  单核与 SMP 两 leg 均为 938 passed，0 failed。
- SMP AP wake/readback 与 TLB shootdown IPI 均 PASS。
