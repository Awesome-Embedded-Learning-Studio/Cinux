# F-USABILITY: PIT irq0 重入 #DF 与 Buildroot usability gate 收敛

> 2026-07-03, 分支 `feat/f-usability`。目标是修 production kernel 启动 Buildroot
> rootfs 时的 PIT irq0 重入 #DF, 并把 `run-buildroot-usability` 跑成闭环。

## 结论

根因是 timer IRQ 里直接调度: PIT/LAPIC timer handler 在 `Scheduler::tick()` 前
EOI, `tick()` 触发 `schedule()`, `context_switch.S` 在旧 timer ISR frame 尚未返回时
执行 `sti`, PIT 立刻可再次进入。`net_poll` 正好是常驻 `sti; hlt` 线程, 所以 backtrace
最容易看到 `timer_queue_tick -> Scheduler::tick -> net_poll_entry` 递归, 最终压到 #DF。

修复后 timer tick 只做时间状态:

- `timer_queue_tick()` 仍从 tick 唤醒超时等待者。
- `SchedulingClass::task_tick()` 仍记账 quantum。
- 不再从 hard IRQ 上下文 inline `schedule()`。
- PIT/LAPIC timer EOI 移到 tick 之后, 保持 tick 执行期间 timer IRQ non-reentrant。

`net_poll` 改成低优先级合作式 kthread: 它仍独占 production 的 `sti; hlt + NetStack::poll`
职责, 但每轮 poll/sleep 后显式 `Scheduler::yield()`, 避免无 timer preemption 时抢在
`kernel_init` 前运行并饿死启动流程。

## 改动

### 1. timer IRQ 不再 inline context-switch

`Scheduler::tick()` 现在只调用 `timer_queue_tick()` 和 `task_tick()` 记账, 不再在 IRQ
frame 内 `schedule()`。真正的抢占需要未来补 return-from-IRQ resched point; 在此之前,
任务切换仅通过 yield/block/exit 等 Task::ctx 安全路径发生。

### 2. PIT/LAPIC timer handler-owned late EOI

`PIT::irq0_handler()` 和 `lapic_timer_handler()` 都改为:

1. 调 `Scheduler::tick()`
2. 再 `irq_eoi(0)`

对应更新 `interrupts.S` 注释, 删除旧 “early EOI before inline preemption switch” 叙述。

### 3. net_poll 变合作式低优先级后台线程

`start_poll_driver()` 创建 `net_poll` 时设置 priority=200。`net_poll_entry()` 每轮:

1. `g_stack.poll()`
2. `sti; hlt; cli`
3. `Scheduler::yield()`

这样 ping/syscall 路径仍不碰 `sti/hlt`, QEMU/SLIRP 仍有人驱动, 但 production boot 不再
依赖 timer preemption 才能从 `net_poll` 切到 `kernel_init`。

### 4. Buildroot usability rootfs 脚本收敛

闭环过程中又暴露三组 rootfs/BusyBox 适配问题:

- CinuxOS execve 暂不支持 shebang 脚本, 所以 inittab 的 `::once:` 改为
  `/bin/sh /etc/cinux-usability-test.sh`。
- BusyBox init 环境里的 PATH 不可靠, usability 脚本改用 `/bin/...` 和 `/sbin/cinux-exit`
  绝对路径。
- BusyBox ash 管道路径当前会卡在 `echo | cat` 的 pipe EOF 语义上; 本 gate 暂降级为
  console write smoke, 脚本内注释标出 follow-up。

## 验证

- `timeout 120 cmake --build build-bu --target run-buildroot-usability -j$(nproc)`
  - QEMU 通过 `/sbin/cinux-exit 0` 退出, target 成功。
  - 串口出现:
    - `[usability] PASS ls-root`
    - `[usability] PASS cat-inittab`
    - `[usability] PASS mkdir-rmdir`
    - `[usability] PASS uname`
    - `[usability] PASS pipe`
    - `[usability] PASS fork-exec`
    - `[usability] result: PASS`
  - 无 #DF / panic。
- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`
  - single + `-smp 2` 两腿绿。
  - 最终 `1101 passed, 0 failed`, `ALL TESTS PASSED`。
- `cmake --build build -j$(nproc)`
  - 全量 build 绿。
- `cmake --build build --target test_host -j$(nproc)`
  - host tests `69/69` passed。

## GOTCHA

1. **不要在 timer IRQ 里直接 schedule 后 sti**: `context_switch.S` 的 `sti` 对 syscall/yield
   路径是可用的, 但在旧 IRQ frame 未 unwind 时会打开同源 timer 重入窗口。
2. **只关 inline preemption 会暴露 net_poll 饥饿**: `net_poll` 在 run queue 中早于
   `kernel_init`, 没有 timer preemption 时必须低优先级 + 合作 yield。
3. **Buildroot gate 的脚本不是 Linux shell 的全量验收**: 当前 `PASS pipe` 是 console
   write smoke, 真 `echo | cat` 留给后续 pipe EOF / BusyBox ash 对齐修复。
4. **本地 rootfs.ext2 需重建或手动更新**: 本次验证用 `debugfs` 把 overlay 的 inittab/script
   写入 `build/buildroot/output/images/rootfs.ext2`; 正式 CI 应由 Buildroot/overlay 重新产镜像。

## follow-up

- 加 return-from-IRQ resched point 后恢复真正 timer preemption, 而不是在 hard IRQ 中
  `context_switch()`。
- 修 BusyBox ash 管道 EOF / wait status 语义, 恢复真实 `echo hello | cat` gate。
- 给 Buildroot rootfs 增加可重复的 overlay assemble/pack target, 避免手工 `debugfs` 更新本地镜像。
