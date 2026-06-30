# F8-M1 批1 — Pipe 写已关读端返 BrokenPipe 触发 SIGPIPE

> 2026-06-30，worktree `worktree-f8-pipe-fifo`（从干净 main `c0188cd`）。F8-M1 第一批。

## 背景

匿名 pipe 的写端 InodeOps 适配层 `PipeWriteOps::write`（kernel/ipc/pipe_ops.cpp）在
`Pipe::write()` 返回负（reader 已关）时，统一返 `Error::IOError`。`to_errno(IOError) == kEio`，
而 `sys_write`（kernel/syscall/sys_write.cpp:53-59）只在 `err == kEpipe` 时投 SIGPIPE：

```cpp
if (err == kEpipe) { signal_send(Scheduler::current(), Signal::kSigpipe); }
```

→ **匿名 pipe 写已关读端，SIGPIPE 从未真触发**（走 IOError→kEio，绕过 kEpipe 分支）。
这是「代码在、机制没真生效」的典型（F-VERIFY audit 同类盲区）。

## 目标

reader-gone 时返 `Error::BrokenPipe`（→ kEpipe → SIGPIPE 真触发），**零新接口**。

## 设计 / 决策

`PipeWriteOps::write` 在 `pipe_->write()` 返负时，用 `pipe_->reader_alive()` 区分两种 -1：

```cpp
int64_t n = pipe_->write(static_cast<const char*>(buf), count);
if (n >= 0) return n;
// Pipe::write returns -1 both for reader-gone and (defensive) invalid arg.
return pipe_->reader_alive() ? Error::InvalidArgument : Error::BrokenPipe;
```

零新接口：复用现成的 `Pipe::reader_alive()` 查询，不动 `Pipe::write` 的返回契约。

## 陷阱

**负测验头跑就 KERNEL PANIC（#GP err=0）**：测试用 `cinux::proc::Task self;`（默认初始化）做
SIGPIPE 投递目标。`signal_send` 行 137 无条件解引用 `target->sig_actions`（指针），而默认初始化
的 Task 把 `sig_actions` 留成垃圾非规范地址 → 解引用 → 非规范地址访问 → **#GP err=0**（不是 #PF：
非规范地址段违规，err=0）。debugcon `FIRST #GP rip=vkprintf` 误导（vkprintf 在读别的 %s；真因是
signal_send 解引用 sig_actions）。

正解（对齐 test_signal.cpp）：`Task self{};`（值初始化，sig_pending=0）**+**
`self.sig_actions = SharedSigActions::create();`。`Task self;`（无 `{}`）是默认初始化，POD 成员
（sig_actions 指针）不确定——注释别写错成「value-initialised」。

## 验证

- 负测验 `test_sys_pipe_sigpipe_on_broken_write`：close reader → `do_write_kernel(wfd,"x",1)`
  → 断言返 `-kEpipe` **且** `sig_is_member(self.sig_pending, kSigpipe)`。经 `CurrentTaskGuard`
  把 `Scheduler::current()` 指向 scratch Task；fds 装 `g_global_fd_table()`（`do_write_kernel`
  经 `current_fd_table()` 解析，harness 里即全局表）。
- host 单测 `test_sys_pipe: write returns -1 after close_reader` 收紧成断言
  `error() == Error::BrokenPipe`（ops 层证 fix）。
- `run-kernel-test-all` 两 leg **987/0**（基线 986 + 1 新 SIGPIPE 测），0 panic。
- host ctest：pipe / sys_pipe 全绿。

## 下一批

批2：真调度阻塞替 sti/hlt 自旋（致命 GOTCHA sti-in-syscall→#DF）+ O_NONBLOCK→EAGAIN。
