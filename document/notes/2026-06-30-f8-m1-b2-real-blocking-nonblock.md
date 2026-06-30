# F8-M1 批2 — Pipe 真调度阻塞替 sti/hlt 自旋 + O_NONBLOCK

> 2026-06-30，worktree `worktree-f8-pipe-fifo`。F8-M1 第二批（头号风险批）。

## 背景 / 目标

匿名 `Pipe::write/read` 阻塞原实现：`irq_enable(); for(1M){ hlt(); irq_disable(); 探; irq_enable(); }`
自旋。这在 `sys_write → do_write_kernel → pipe->write` 的 **syscall 上下文里 sti**——致命 GOTCHA
（见 memory `sys-ping-df-sti-in-syscall`）：sti 窗口里 LAPIC tick 抢 `%gs:0` 栈陷阱帧 → sysretq
弹花 → **#DF（真硬件必炸）**。harness 不真跑 ring3 阻塞，故 931/0 假绿掩盖着。

目标：① 阻塞改真调度 wait queue（**别 sti/hlt**）② 加 O_NONBLOCK→EAGAIN。

## 设计 / 决策

**阻塞机制复用现成 proven 模板**（推翻旧 todo 的 ConditionVariable 方案）：`prepare_to_wait()` +
`schedule_blocked()` + `unblock()` + `Task::wait_next` 内侵式等待队列——`Mutex::lock`
（kernel/proc/sync.cpp）和 console TTY 阻塞读（drivers/tty/console_tty.cpp）已验，lost-wakeup-safe
跨核。Pipe 加两条队列 `read_waiters_/write_waiters_`：

- write 满 / read 空：`lock_.irq_guard()` 下 enqueue self + `prepare_to_wait`（Blocked 翻转原子 vs
  对端 CPU，无 lost wakeup），**放锁放中断** → `schedule_blocked()`；对端排空/写入后 `wake_one`。
- close_reader → `wake_all(write_waiters_)`（writer 重试见 reader gone→BrokenPipe/SIGPIPE）；
  close_writer → `wake_all(read_waiters_)`（reader 见 EOF）。
- `wake_one` 在持锁下调 `unblock`：安全（pipe 非互斥，不交接所有权，被唤醒任务之后重新获取锁；
  scheduler run-queue 锁绝不会跨 pipe 锁获取 → 无 AB-BA）。

**O_NONBLOCK**：`Pipe::read/write` 加 `bool nonblock`（默认 false，旧 2-arg 调用不破）；满/空且
nonblock 返哨兵 `PIPE_WOULDBLOCK(-2)`，ops 层（`PipeReadOps/PipeWriteOps`）映射 `Error::WouldBlock`
（→ kEagain）。ops 加 `nonblock_` 成员 + 构造参（sys_pipe 匿名端始终 blocking；O_NONBLOCK 经 FIFO
open 落地见 M2）。**不改 InodeOps::read/write 签名**（blast radius 大，PTY 刚动过）。

## 陷阱

**host 兼容（pipe.cpp 链进 host 单测，无 scheduler）**：Scheduler 调用 + Task::wait_next 访问 +
`process.hpp/scheduler.hpp` include + wait-queue helper 全包 `#ifndef CINUX_HOST_TEST`，对齐 irq.hpp
现成 host-guard（§14 rule 3：host 测试 vs 真内核是合法 #ifdef）。host 路径不阻塞（`#else break`），
push/pop 多少返多少——同步单测本就不触发满/空。

**`need_block` 未用变量警告（host）**：host 下 `need_block` 声明了但赋值/读取都在 `#ifndef` 分支被
编译掉 → `-Wunused-variable`。fix：声明也包 `#ifndef CINUX_HOST_TEST`（零警告门禁）。

## 验证

- 负测验：`test_pipe_nonblock_write_full_returns_wouldblock`（满 pipe nonblock 写→PIPE_WOULDBLOCK）、
  `test_pipe_nonblock_read_empty_returns_wouldblock`（空 pipe nonblock 读→PIPE_WOULDBLOCK；writer 关后→0 EOF）、
  `test_pipe_write_ops_nonblock_full_is_wouldblock`（ops 映射 WouldBlock）。
- **真阻塞唤醒的正确性**靠对齐 Mutex/console_tty 模板 + 代码审查（harness 单线程没法确定性测两任务阻塞
  唤醒；负测验守住 nonblock 语义 + 旧 24 个 pipe 测零回归）。
- `run-kernel-test-all` 两 leg **990/0**（批1 的 987 + 3 新 nonblock 测），0 panic / 0 #DF。
- host：pipe / sys_pipe 全绿，零警告；kernel/host 全量编零警告。

## 下一批

批3：命名 FIFO 核心（`fifo.{hpp,cpp}`：FifoRegistry + FifoOps，`open()` cloning 首读者建 Pipe）。
