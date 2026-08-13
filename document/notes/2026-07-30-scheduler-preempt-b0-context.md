# 调度器任意内核态抢占 B0：安全切换底座

> 日期：2026-07-30 · 分支：`feat/nvme-async-io` · 状态：B0 完成

## 审查结论

现有修改只让 timer、unblock 和 reschedule IPI 写入 `need_resched`，没有安全的消费点，因此并未实现
内核态抢占。把 `schedule()` 直接放进 `signal_check_deliver_isr()` 也不成立：

1. 硬件 IRQ 统一使用 IST2，而 IST2 原先是每 CPU 共享的 4 KiB 栈。挂起 IRQ 返回帧后切走，下一次
   IRQ 会从同一栈顶覆盖该帧。
2. `context_switch.S` 无条件执行 `sti`。从 IRQ-exit 切走会在旧 IRQ 尚未 IRET 时打开中断，形成递归
   IRQ/双重异常窗口。
3. `preempt_count` 是 per-CPU 状态，不能由可睡眠、可迁移的 Mutex 持有者跨
   `schedule_blocked()` 保存；否则接班任务继承非零计数，迁移后的解锁还会在另一 CPU 上错误递减。
4. Spinlock 的计数必须在发布锁所有权前增加，在实际 unlock 后减少，才能封住两个抢占竞态窗口。

## B0 实现

- `CpuContext` 原保留槽 `gs_base` 改为 `rflags`，`context_switch` 保存/恢复 IF。恢复 IF=1 使用
  `sti; jmp` 的 interrupt shadow，确保新任务上下文安装完成前 IRQ 不可见。
- fresh kernel task 初始化 IF=1；user fork child 保持 IF=0 直到 `ret_from_fork` 自己的
  `cli/swapgs/sysretq` 尾；kernel fork child继承调用点 IF。
- 每个 Task 分配独立 8 KiB IRQ 栈及 guard page，fork/clone 单独分配，任务回收同时释放 kernel 与
  IRQ 两套栈。
- GDT 增加 per-CPU IST2 更新接口；每次任务切换及 AP idle 首次接管时，把 IST2 指到 incoming
  task 的 IRQ 栈。GDT 内嵌 IRQ 栈只承担调度器接管前的 bootstrap。
- Spinlock 与 InterruptGuard 维护 per-CPU `preempt_count`；Mutex 保持纯任务所有权，不触碰该计数。
- 保持 `Task::fpu_state` 在 +384：IRQ 栈字段放在 FPU 区之后，继续满足 syscall/interrupt 汇编的
  hardcoded FXSAVE ABI。

B0 尚未启用 IRQ-exit `schedule()`，所以本批只改变资源生命周期与上下文保存能力，不改变调度时机。

## 验证

```text
cmake --build build --target big_kernel_test -j$(nproc)
timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)
```

结果：

- 单核 937/937，0 failed；
- SMP 937/937，0 failed；
- AP wake/readback 与 TLB shootdown IPI PASS；
- NVMe、fork/clone、同步原语与 context switch 机制测均通过。

下一批 B1 在 IRQ 汇编的 outermost return 点消费 `need_resched`，并只在
`preempt_count == 0` 时切换。
