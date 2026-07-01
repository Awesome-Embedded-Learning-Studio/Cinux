# F-ECO 批3 + 批4:nanosleep + dup/dup2/fcntl

> 2026-07-01。外包 worktree `feat/outsource-f-eco-b3-b4`(从集成线 `9ef98e0`),cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`(`3be9859` 批3 + `b271439` 批4)零冲突。两 leg **1047/0**(1037+3+7)。**未 push**。
> busybox 试金石第三、四刀的**内核件**:[todo f-eco/00-busybox-touchstone](../todo/f-eco/00-busybox-touchstone.md) 批3(`cat/head/grep/sed` 负载 + `sleep`)、批4(`sh` + 管道 + 重定向)。负载验收(跑 busybox applet)需 CI build busybox,本弧只交付 **syscall 内核实现 + 机制测**;busybox applet 端到端留 CI。

## 批3:nanosleep(35)

- `sys_nanosleep` / `do_nanosleep_kernel`(复用 [sys_clock_gettime](../../kernel/syscall/sys_clock_gettime.cpp) 的 HPET monotonic + PIT 兜底,5 行 helper 复制避跨文件耦合)。deadline = `monotonic_ns() + req`;`while (now < deadline) Scheduler::yield()`。
- **不 sti/hlt**(sti-in-syscall→#DF;yield 经 Task::ctx 安全,同 net_init pump_yield)。**不 IRQ wake**(HPET 周期中断是 F5-M4 follow-up;现 poll+yield 正确不高效)。信号打断(-EINTR + rem)不做:请求时长总是睡满,rem 清零。busybox 整秒 sleep 够用。
- **do_ 内核变体**供 ring0 测试直调([user_access.hpp](../../kernel/arch/x86_64/user_access.hpp) 的 `is_user_vaddr` 拒内核栈地址 → sys_nanosleep 在测试内核过不了 copy_from_user,见 [[f8-m3-unix-socket-progress]] GOTCHA)。

## 批4:dup(32) / dup2(33) / fcntl(72)——sh + 重定向质变点

- **FDTable::dup / dup2**([file.cpp](../../kernel/fs/file.cpp)):每次复制 = **new File**(独立 open file description,拷 inode+offset+flags+cloexec)——hobby-OS 简化避 File 引用计数;新 fd 达同 inode(pipe/重定向够用)。Linux 共享描述符(共享 offset)留 follow-up。dup 保留 stdin/out/err(从 `FD_FIRST=3` 起,hobby 偏差,测试确定性);dup2 可指任意 fd(含 0/1/2 重定向)。dup2 先 close 目标占位者再拷;`dup2(fd,fd)` 合法同 fd → no-op。
- **File +cloexec 字段**(fcntl FD_CLOEXEC;execve 暂不消费,留 follow-up)。
- **sys_fcntl 子集**:F_DUPFD(0,lowest fd≥arg)/ F_GETFD(1)/ F_SETFD(2,cloexec)/ F_GETFL(3,access mode)。F_SETFL(4)/未知 cmd→**EINVAL**(O_NONBLOCK 运行时改需 File 状态字段 + 接 pipe/socket ops;现 nonblock 在 FIFO open() 时定型,留 follow-up)。
- sys_dup 用 `get()+dup()` 区分 -EBADF(bad oldfd)/ -EMFILE(表满);get+dup 是 hobby 规模下无害的 TOCTOU。

## 机制测试(防假绿,10 测)

- **批3**(3):duration(HPET delta≥5ms 且<500ms 防挂)+ zero(立即返)+ bad-nsec(≥1e9→EINVAL)。
- **批4**(7):dup inode 共享 + **byte round-trip**(经 InodeOps 内核缓冲直驱 pipe write/read;sys_write 在 ring0 测过不了 is_user_vaddr)+ dup2 redirect round-trip(target=42→pipe,写经 target 读经 readfd)+ dup2 same-fd no-op + fcntl cloexec round-trip + F_DUPFD(min-fd)+ F_GETFL(WRONLY=1/RDONLY=0)+ bad-fd→EBADF。

## GOTCHA(机制测试抓的——测间 fd 表污染)

- 第一轮 7 个批4 测全 PASS,但 `test_ramdisk::test_vfs_close_invalid_fd` 红:`close(42)` 期望 -1,实际 0。根因:**测试内核 current_fd_table() 是整个 suite 共享的一个 global 表**,批4 `test_sys_dup2_redirects_to_target` 把 fd 42 占了(dup2 target=42)且没清 → 后续测 `close(42)` 看到 fd 42 有效 → 返 0 ≠ -1。**测间状态污染**。
- 修法:每个批4 测结束 `cleanup_fds()` close 掉它创建的全部 fd(pipe 两端 + dup 结果)。启示:**ring0 测试共享全局 fd 表,凡开 fd 的测都须收尾 close**,否则污染后续测(易误判为被测代码回归——实际是前测脏状态)。

## follow-up(留后续)

- **busybox applet 端到端验收**:cat/head/grep/sed 负载 + sleep + sh/管道/重定向,需 CI build busybox -O2(本地无 build)。
- **dup 共享描述符语义**:现独立 File(独立 offset);Linux dup 两 fd 共享 offset(同 open file description)。需 File 引用计数。
- **execve 消费 FD_CLOEXEC**:现 cloexec 仅存取,execve 不扫描 fd 表关 cloexec fd。
- **fcntl F_SETFL / O_NONBLOCK 运行时改**:需 File 状态字段 + pipe/socket ops 读它(现 nonblock 在 FIFO open() 定型)。
- **nanosleep IRQ wake + -EINTR**:HPET 周期中断(F5-M4 follow-up)后,信号打断返 -EINTR + rem。
- **dup 范围**:现 dup 从 FD_FIRST 起(hobby 偏差),Linux 从 0。

## 验证

`run-kernel-test-all` 两 leg:单核 1047/0 → -smp 2 1047/0 → ALL TESTS PASSED(基线 1037 + 3 nanosleep + 7 dup/fcntl)。`test_host` 69/69(改公共头 [file.hpp](../../kernel/fs/file.hpp):File +cloexec、FDTable +dup/dup2,host mock 零回归)。`check_net_decoupling` 绿。

**push/PR 归用户**——F-ECO 外包线,等回主线。
