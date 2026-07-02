# F-USABILITY: Buildroot busybox 动态启动修复

> 2026-07-02, 分支 `feat/f-usability`。目标是把 Buildroot rootfs 里的动态 busybox init/sh
> 真启动到交互提示符, 同时把内核栈压回 8 KiB 以贴近 Linux, 让潜在栈深问题尽早暴露。

## 结论

这次不是“PIE / 动态链接整体不支持”的单点问题。Buildroot busybox 确实是 ET_DYN PIE,
并通过 `/lib/ld-musl-x86_64.so.1` 进入动态装载器; syscall trace 证明 ldso 已经跑起来,
`/bin/sh` 也走到了 `poll(fd=0, -1)`。之前“零 syscall”的判断只是日志盲区。

真正阻塞是三组 Linux 兼容语义叠加:

1. musl/PIE 会在低地址区做 `MAP_FIXED|MAP_ANON|PROT_NONE`, 旧 `sys_mmap` 只允许高 mmap arena,
   直接 `-EINVAL`。
2. `PROT_NONE` VMA 上的 not-present page fault 不能被 demand-zero 偷偷填页, 否则保护语义失效。
3. FDTable 旧语义从 3 开始分配, 且 PID1 exec 前没有稳定安装 fd 0/1/2, busybox init/sh 的 stdio
   会被 early open/socket 抢走, 最终 sh 退出或 prompt 不可见。

修完后 build-console 日志到:

```
CinuxOS Buildroot init: userspace up
/ #
```

未再出现 #DF / SIG21 default stop / panic / segfault。

## 改动

### 1. kernel stack 固定为 8 KiB

`TaskBuilder::STACK_PAGES` 改为 2, 并加 `static_assert(STACK_PAGES == 2)`。以后如果栈深爆了,
修栈深和大对象, 不靠悄悄加栈页绕过。

同时把本批碰到的测试大栈对象移到堆上: `test_syscall.cpp` 的 scratch `Task` 不再压 1 KiB+
栈帧, 完整测试日志无 `frame size` warning。

### 2. MAP_FIXED 低地址兼容 PIE/musl

`sys_mmap` 的 `MAP_FIXED` 校验改为允许非空、页对齐、低于 `USER_STACK_TOP` 的用户地址范围,
不再强制落在 `USER_MMAP_BASE..USER_MMAP_END`。这匹配 ldso/PIE 的实际布局需求:
装载器可以显式选择低地址保护洞或映射窗口。

### 3. PROT_NONE / 权限 fault 不再误 demand-zero

`page_fault.cpp` 在 extable 和普通用户 not-present fault 前先检查 VMA 权限:

- 写 fault 需要 `VM_WRITE`
- instruction fault 需要 `VM_EXEC`
- 读 fault 需要 `VM_READ`

找得到 VMA 但权限不允许时, 对用户态发 SIGSEGV; 不再把 `PROT_NONE` 页补成可读写零页。
这让 musl 的 guard/probe 语义和 Linux 更接近。

### 4. FDTable 对齐 Linux 从 0 分配

`FDTable::alloc()` 现在返回最低空闲 fd, 包括 0/1/2; `dup(oldfd, min_fd)` 从 `max(min_fd, 0)`
开始扫, 不再强制跳过 stdio。host/unit 和 in-kernel 测试同步更新。

踩到的连带点: 旧测试共享全局 fd table, 从 0 分配后更容易污染 legacy stdio fallback。
`test_poll` / `test_syscall` 加了 scratch fd table guard, 让测试断言只验证自己的语义。

### 5. PID1 exec 前安装 /dev/console 到 fd 0/1/2

Linux fd 分配语义切过来后, “没有 stdio” 会变得更危险: init 早期 `socket/open` 会自然拿到 fd0,
然后 `/bin/sh` 的 stdin/stdout/stderr 就不再是控制台。`shell_launch.cpp` 在 exec `/sbin/init`
前创建新的 FDTable, 把 `/dev/console` 安装到 0/1/2。

`devfs` 为此暴露 `console_inode()`, `/dev/tty` 在没有 controlling tty 时也可回落到 console,
`TIOCSCTTY` 会把当前 task 标成 console controlling tty, 并在必要时设置 foreground pgid。

### 6. legacy stderr fallback

`do_write_kernel` 的无 File fallback 从 fd1 扩到 fd1/fd2。真实进程已有 fd table 时优先走 inode,
legacy fallback 只服务早期/测试上下文, 与 read/ioctl/poll 对 absent stdio 的 console fallback
保持一致。

## GOTCHA

1. **“零 syscall”是假象**: 临时 syscall trace 显示 `/bin/sh` 已经执行
   `arch_prctl/set_tid_address/brk/mmap/mprotect/uname/getcwd/ioctl/open/read/lseek/close/poll`。
   后续遇到“没有日志”先补边界 trace, 别直接推成 ldso 没启动。
2. **PIE 不是坏事, 低地址 MAP_FIXED 才是闸口**: Buildroot busybox 是 ET_DYN PIE,
   interpreter 是 musl ldso。CinuxOS 已能加载 ET_DYN+interp, 但 `MAP_FIXED` 策略过窄。
3. **PROT_NONE 必须真 none**: demand paging 不能只看“VMA 存在”。权限错的 not-present fault
   也要按保护错误处理, 否则 guard page 失效且行为偏离 Linux。
4. **FD 0/1/2 不能靠运气**: 一旦 FDTable 从 0 分配, PID1 必须显式安装 stdio。否则第一个
   open/socket 会偷走 fd0, shell 交互现象会变成“偶发没 prompt / 直接退出”。
5. **测试别共享脏 fd table 猜 fd 号**: Linux 语义下 0/1/2 都是普通可分配槽。需要 legacy
   fallback 的测试用空 scratch table; 需要普通 invalid fd 的测试要避开 absent fd 0/1/2。

## 验证

- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`
  - single-CPU + `-smp 2` 两腿均 `ALL TESTS PASSED`
  - 最终计数 `1101 passed, 0 failed`
  - 日志无 `frame size` warning
- `timeout 90 cmake --build build-console --target run`
  - QEMU 被 timeout 杀掉是预期
  - 串口出现 `CinuxOS Buildroot init: userspace up` 和 `/ #`
  - 未出现 #DF / SIG21 / panic / segfault
- `cmake --build build --target test_host`
  - host tests `69/69` passed

## follow-up

- 真正的 controlling tty/session 语义还只是 console MVP, 后续 getty/login/PTY 会需要更完整的
  session leader / foreground pgid 规则。
- `run` 路径仍有不少 execve/COW/VMM 调试日志, 后续可按 usability CI 需要降噪。
- Buildroot 接管阶段还未完成; 本批只是把动态 busybox base rootfs 的内核兼容阻塞清掉。
