# F8-M2 批5 — FIFO 跨 fd round-trip 真测 + F8-M1/M2 收官

> 2026-06-30，worktree `worktree-f8-pipe-fifo`。F8-M2 收官批。

## 目标

mkfifo → open 写端 → open 读端 → write/read 跨 fd round-trip → close 端到端内核真测，
锁死 F8-M1（pipe 增强）+ F8-M2（命名 FIFO）。

## 测试（kernel/test/test_fifo.cpp）

`run_fifo_tests`（3 例，经 do_mknod_kernel/do_write_kernel/do_read_kernel 真路径）：
1. `test_fifo_mkfifo_open_roundtrip`：do_mknod_kernel("/dev/kfifo", S_IFIFO|0666)→0；
   FifoRegistry::lookup_inode→FIFO inode；FifoOps::open cloning 出写端(O_WRONLY)+读端(O_RDONLY)，
   装 g_global_fd_table；do_write_kernel(wfd,"fifort",6)=6 → do_read_kernel(rfd,buf,6)=6 内容核对。
   证 mkfifo + 首读者建共享 Pipe + 跨 fd round-trip 端到端通。
2. `test_fifo_mknod_non_fifo_rejected`：S_IFCHR mode → -ENOSYS（本里程碑只做 FIFO）。
3. `test_fifo_mknod_duplicate_eexist`：重复 mkfifo 同名 → -EEXIST。

## 决策

测试内核**未全局挂载 /dev**（test_devfs 用局部 DevFs；main_test boot 只 vfs_mount_add ext2 到 /），
故本测直查 FifoRegistry::lookup_inode（DevFS dynamic_lookup 运行时返的就是这个 inode），不走 VFS
路径解析。DevFS dynamic_lookup 的 4 行胶水（FIFO 先查 → 落 PTY）由 host test_fifo + 代码审查 +
两 leg 零回归守住（它在 /dev 每次查找的热路径上，PTY/console 全绿证对它无影响）。

## 验证（F8 全里程碑）

- `run-kernel-test-all` 两 leg **993/0**（基线 986 + 批1 SIGPIPE 1 + 批2 nonblock 3 + 批5 FIFO 3），
  0 panic / 0 #DF。单核 + -smp 2 两套全绿。
- host：test_fifo（4/0）+ pipe/sys_pipe/devfs/pty/fd_table/vfs_mount 全绿。
- 关键里程碑修复实证：①匿名 pipe SIGPIPE 真触发（批1 负测验）②pipe 阻塞消除 sti-in-syscall→#DF
  隐患（批2，对齐 Mutex/console_tty wait queue）③O_NONBLOCK→EAGAIN（批2/3）④命名 FIFO mkfifo+open
  cloning round-trip（批5）。

## 范围栅栏（诚实 follow-up）

- open()-级两端就绪阻塞推迟（数据级阻塞保正确）；close 不销毁 pipe 开新 epoch（无 InodeOps::release）；
  per-open ops/inode close 泄漏（同匿名 pipe hobby-OS 限制）。
- FIFO flat 命名空间落 /dev；任意路径 mkfifo + 用户态 shell 真闭环留后续（需 do_openat 也调 open() cloning）。
- ConditionVariable 抽象留 sync 里程碑（本里程碑复用现成 wait queue）。

## 收官

F8-M1（Pipe 增强：BrokenPipe/SIGPIPE + 真调度阻塞 + O_NONBLOCK）+ F8-M2（命名 FIFO：FifoRegistry +
FifoOps cloning + sys_mknod）全 ✅。5 commit（c14f4c5 docs / b1ae6ec / 9d15206 / 16b30bf / cb1864a +
本批）。**push/PR 归用户**。
