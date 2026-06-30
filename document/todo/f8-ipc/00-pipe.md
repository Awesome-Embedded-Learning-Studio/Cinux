# M1: Pipe 增强（BrokenPipe 语义 + 真调度阻塞 + O_NONBLOCK）

> F8-M1。匿名 pipe 现状(kernel/ipc/pipe.{hpp,cpp} + pipe_ops.{hpp,cpp}):4KB RingBuffer
> + Spinlock;阻塞=sti/hlt 自旋(PIPE_SPIN_WAIT_ITERS=1M,与 sys_read 同款);reader-gone 时
> `PipeWriteOps::write` 返 `Error::IOError`(→kEio),故 sys_write.cpp:53-59 的 EPIPE→SIGPIPE
> **从未真触发**(匿名 pipe 走 IOError≠BrokenPipe≠kEpipe)。三件事修齐。
>
> 阻塞机制选型(推翻旧 todo 的 Mutex+ConditionVariable 方案):复用**现成的
> prepare_to_wait/schedule_blocked/unblock + Task::wait_next 内侵式等待队列**
> (sync.cpp Mutex/Semaphore、drivers/tty/console_tty.cpp 已验,lost-wakeup-safe 跨核)。
> ConditionVariable 抽象留后续 sync 里程碑,本里程碑不扩 sync.hpp。

## 任务清单

### T1: BrokenPipe 语义(reader-gone → SIGPIPE 真触发)

**文件**: `kernel/ipc/pipe_ops.cpp`

- [x] `PipeWriteOps::write`:`pipe_->write()` 返负时,按 `pipe_->reader_alive()` 区分
      `Error::BrokenPipe`(reader gone → kEpipe → sys_write 投 SIGPIPE)vs `Error::InvalidArgument`(buf 非法)。
- [x] 负测验(kernel test):close reader → `do_write_kernel` → 断言返 `-kEpipe` **且**
      `sig_is_member(current->sig_pending, kSigpipe)`(经 CurrentTaskGuard 设 current,证 SIGPIPE 真投递)。

### T2: 真调度阻塞替 sti/hlt 自旋

**文件**: `kernel/ipc/pipe.{hpp,cpp}`

> **致命 GOTCHA(sti-in-syscall→#DF,见 memory sys-ping-df-sti-in-syscall)**:
> 现状 `write/read` 在(sys_write→do_write_kernel→)syscall 上下文里 `irq_enable();hlt()` 自旋 →
> LAPIC tick 抢 %gs:0 栈陷阱帧 → sysretq 弹花 → #DF(真硬件必炸,harness 抓不到因不真跑 ring3 阻塞)。
> 正解:**别 sti/hlt 自旋**,走 prepare_to_wait/schedule_blocked(对齐 console_tty/Mutex)。

- [ ] Pipe 加两条内侵式等待队列(`Task* read_waiters_/write_waiters_` + Task::wait_next)+ wake_one/wake_all helper。
- [ ] write 满:lock.irq_guard() 下 enqueue self → prepare_to_wait,放锁放中断 → schedule_blocked;reader 排空后 wake_one(writer)。
- [ ] read 空:对称;writer 写入后 wake_one(reader)。
- [ ] close_reader → wake_all(write_waiters_);close_writer → wake_all(read_waiters_)。
- [ ] **host 兼容**:Scheduler 调用包 `#ifndef CINUX_HOST_TEST`(对齐 irq.hpp 现成 host-guard 模式;§14 rule 3 合法),host 路径不阻塞(同步单测本就不触发满/空)。

### T3: O_NONBLOCK → EAGAIN

**文件**: `kernel/ipc/pipe.{hpp,cpp}` + `pipe_ops.{hpp,cpp}`

- [ ] `Pipe::read/write` 加 `bool nonblock` 参(默认 false,2-arg 旧调用不破);满/空且 nonblock 返哨兵 `kWouldBlock(-2)`。
- [ ] `PipeReadOps/PipeWriteOps` 加 `nonblock_` 成员 + 构造参;ops 映射 kWouldBlock → `Error::WouldBlock`(→kEagain)。
- [ ] sys_pipe 匿名端始终 nonblock=false(pipe(2) 不带 flags,blocking);O_NONBLOCK 经 FIFO open 落地(见 01-fifo.md)。
- [ ] 负测验:nonblock 写满 pipe → `-kEagain`;nonblock 读空(writer 开)→ `-kEagain`。

## 产出物

- [x] `kernel/ipc/pipe_ops.cpp` — BrokenPipe(T1)
- [ ] `kernel/ipc/pipe.{hpp,cpp}` — 真调度阻塞 + O_NONBLOCK(T2/T3)
- [ ] `kernel/test/test_sys_pipe.cpp` — SIGPIPE + EAGAIN 负测验
