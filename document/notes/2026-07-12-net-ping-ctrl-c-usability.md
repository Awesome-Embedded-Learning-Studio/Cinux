# 用户态 ping + Ctrl+C 打断全链通(网络可用化弧)

> 日期：2026-07-11→07-12 · v1.0.0 发版后真实可用化 · 状态：✅ 合 main

## 背景

F7 网络栈在测试内核里早已 `ping 10.0.2.2` 干通，但那是内核态测试路径。发版后在 GUI 桌面用
**busybox ping**（用户态、阻塞在 `recvfrom` 的 SOCK_RAW）才发现一整串信号/作业控制缺陷首次被触发——
custom handler 之前几乎没真投递过。本弧把用户态 ping + Ctrl+C 打断走通。

## 弧线（commit 链）

1. `79996b2` SOCK_RAW + IPPROTO_ICMP：raw socket 把 ICMP echo reply 投递回用户态，ping 能收包。
2. `c60a375` Ctrl+letter 控制码（Ctrl+C = INTR）+ `setitimer` stub（返 0、先占位）。
3. `99e62c7` ping Ctrl+C 打断 + signal EINTR：阻塞 IO 可被信号打断的框架。
4. `ff58be3` PTY 转发 `^C` 到前台 pgrp（GUI 终端经 PTY 行规范投递 SIGINT，对齐 console_tty 的 killpg）。
5. `36798b9` 修 Ctrl+C 打断 ping 的**两个耦合既有 bug**（见下）。
6. `ba895d7` 真 `setitimer(ITIMER_REAL)` 驱动 busybox ping 每秒发包。

## 两个耦合既有 bug（commit 36798b9）

ping 被 SIGINT 唤醒 → recv 返 EINTR → busybox 循环再 `recvfrom` → 又睡死。根因不是一处，是一串：

**坑 1：`schedule_blocked` 不看 pending signal（re-sleep）。** Linux 的 TASK_INTERRUPTIBLE 会因
signal_pending 立即返回不睡；Cinux 的 `schedule_blocked` 无脑睡。那次 unblock（`queue_signal` 里
`wait_queue_head != nullptr && state==Blocked && deliverable` → `Scheduler::unblock`）是**一次性**的，
ping re-sleep 后 SIGINT 仍 pending 但没人再唤 → 永远 Blocked。修（`scheduler_block.cpp`）：
`wait_queue_head != nullptr`（用户态 IO 可中断等待）且 `signal_deliverable_pending` 时**不睡**翻回 Running；
`wait_queue_head == nullptr` 的内核 Mutex/futex/waitpid 保持不可中断（对齐 TASK_UNINTERRUPTIBLE）。判别器
是 `wait_queue_head`：只有 `net::wait_enqueue` / `ipc/pipe` 设它。

**坑 2：`signal_setup_frame` handler 入口 RSP 栈对齐错。** `R = user_rsp - pad - sizeof(SignalFrame)`
漏了 `[R]` 那 8 字节 retaddr 槽 → R%16==0（SysV ABI 要函数入口 RSP%16==8）。handler 一跑 `movaps` 等
SSE 对齐指令就 #GP，被映射成 SIGILL 杀掉（ping Ctrl+C 退出但报 Illegal Instruction）。修：
`R = user_rsp - 8 - pad - sizeof(SignalFrame)`（补 retaddr 槽 -8），R%16 恒为 8。

## setitimer(ITIMER_REAL)（commit ba895d7）

ping 每秒发包靠 setitimer（原 stub 返 0 无 SIGALRM）。实现：Task 加 `itimer_real_value_ns`/
`itimer_real_interval_ns`；`sys_setitimer` copy_in itimerval 转 ns；`itimer_real_tick(delta)` 从
PIT IRQ0（BSP，100Hz）调，遍历注册表扣减，到期 `signal_send(SIGALRM)` + reload interval。PIT 是全局
wall-clock 源（AP LAPIC timer 只管抢占），sleeping task 的 itimer 照走时。锁外 signal_send 防 lockdep。

## 调试链路（给未来的自己）

ping Ctrl+C 不通得沿信号链逐点 trace，别猜：
PTY master_write 没投递信号 → fg pgrp 错（killpg 打中 kernel_init）→ 信号打中但 ping re-sleep（坑 1）
→ handler #GP 栈对齐（坑 2）→ handler 干净跑但 ping 1s 变 0.5s（PIT mode 3，见
[2026-07-12-pit-mode3-to-2-timing](2026-07-12-pit-mode3-to-2-timing.md)）。

定位手段：`signal_dump_registry()` 看谁真收到信号；`[itimer] hpet_ns` 看真 fire 间隔。

## 验证

两 leg run-kernel-test-all 1946/0；GUI 桌面 busybox ping 10.0.2.2 每秒一包 + Ctrl+C 干净退出（无 SIGILL）。

## 教训

- 阻塞 IO 的可中断等待必须有 signal_pending 判别器，不能只靠一次性 unblock。
- 信号 handler 帧的栈布局要满足 SysV ABI 入口对齐（RSP%16==8），漏 retaddr 槽是隐蔽坑。
- 用户态真实负载（busybox ping + job control）是信号/作业控制路径的首块试金石。
