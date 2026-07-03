# F-USABILITY: busybox echo/cat FAIL = 测试夹具 fd 0/1/2 污染(非 stdio 回归)

> 2026-07-03, 分支 `feat/f-usability`。目标是隔离 busybox smoke 里 `bb echo` + `bb cat`
> exit(1) 的根因(交接文档疑似 codex `ec6d4b0` / force_sig `fb42a1d` / worktree 合并引入的
> stdio/sync 回归,准备 git bisect)。

## 结论(颠覆交接猜测)

根因**不是** codex / force_sig / worktree 引入的 stdio/sync 回归,也**不需要** bisect。

真因:**ring0 单元测试共享全局 fd 表(`g_global_fd_table`)且漏 `close`,残留 inode 占了
fd 0/1/2** —— CinuxOS `FDTable` 不保留 stdin/stdout/stderr,所以首个漏 close 的 `open()`
直接拿到 fd 0。busybox smoke 的 child 经 fork(`fork.cpp:416-432` 从 parent / global 表复制)
继承污染表,echo 因 `return fflush(stdout)==0 ? 0 : 1` 把 fd=1 写失败转成 `exit(1)`;
env / hostname / ps / wc / free 不 gate exit on stdout,故 PASS。

**判别点**:echo 的 exit code 挂在 `fflush(stdout)`;其他 applet 不挂。所以"只有 echo/cat
FAIL"不是 stdio 整体坏,而是这两个 applet 把 stdout 写失败显式转成 exit code。

## 诊断路径(三步,全部加临时诊断后还原)

1. **sys_write 入口诊断**(对 fd≤2 打 `ret`):抓到 `[W] fd=1 n=17 ret=-22(EINVAL)`。
   关键反证:env / hostname 的 fd=1 write **同样 -22**,但它们 PASS → 问题在 exit-code 语义,
   不在"echo 写不了"。
2. **do_write_kernel 失败诊断**(打 inode type/offset):`[DW!] fd=1 err=22 type=1(Regular)
   off=0` → fd=1 指向一个 ext2 Regular inode(不是 console)。
3. **smoke entry + echo smoke 前 dump fd 0/1/2**:smoke entry 时 fd 0/1/2 **已被三个
   Regular inode 占**(`fsp` 各异:fd0/1 非 null、fd2 null),echo 时不变 → 污染在 smoke
   **之前**(单元测试残留),不是 hello/echo 之间。

附:`Ext2FileOps::write` 的 EINVAL 来自 `inode->fs_private == nullptr` 分支
([ext2_common.cpp:146-148](../../kernel/fs/ext2/ext2_common.cpp#L146));fd=2(fs_private=null)
却返 ENOSYS(38) 说明它 ops 已不是 ext2 —— 三个槽是**不同测试残留的异质 inode**,各自走
不同 ops、返不同 errno。

## 改动(commit `7e5f3b6`)

`musl_hello_smoke_entry` 开头 `close` fd 0/1/2(harness 自保):

```cpp
cinux::fs::current_fd_table().close(0);
cinux::fs::current_fd_table().close(1);
cinux::fs::current_fd_table().close(2);
```

child 继承干净表 → fd=1 落 `nullptr` → `do_write_kernel` 走 legacy console `kprintf` 路径
([sys_write.cpp:69-75](../../kernel/syscall/sys_write.cpp#L69)) → 串口输出 → echo fflush
成功 → exit 0。

这是**治标**(测试夹具自保)。治本(找漏 cleanup_fds 的具体单测 / FDTable 是否该保留 0/1/2)
留 follow-up —— 治标先解锁 smoke 绿,治本独立批。

## 验证

`run-kernel-test-all` 两 leg `1108/0` + busybox `14/14` PASS(echo/cat/env/hostname/ls/
uname/id/whoami/pwd/true/false/sleep/wc/ps/free 全 exit 0)。串口真看到 busybox `ls` 输出
目录列表(`bin/etc/hello/hello.txt/lost+found/sbin`,带 ANSI 颜色)。

## 教训

- **"只有部分 applet FAIL"先看 exit-code 语义差异**(echo 挂 fflush,其他不挂),而非默认
  stdio/sync 回归。本次交接文档的"疑似 stdio"猜测差点引向无意义的 bisect。
- **测试内核共享全局 fd 表**:凡开 fd 的 ring0 单测须 `cleanup_fds`(memory 老 GOTCHA,
  `f-eco-b3-b4-nanosleep-dup-fcntl` 同族),否则污染低号 fd 0/1/2,smoke child 经 fork 继承。
- **CinuxOS `FDTable` 不保留 stdin/stdout/stderr** —— 这是污染能落到 fd 0 的设计前提,
  评估是否该改(follow-up)。
- 诊断顺序值得记:先证明"同路径别的程序 PASS"(env 同样 -22 但 PASS)→ 立刻排除"路径坏了",
  转向"exit-code 语义";再 dump fd 表找污染时机。比直接 bisect 快一个数量级。

## Follow-up

1. **找漏 cleanup_fds 的具体单测**:在 `kernel_main` 的 `run_xxx_tests` 序列里二分 dump
   fd 0/1/2,或在 `FDTable::set` 加 fd≤2 的 log 定位调用者。memory 提示 `test_vfs_close_invalid_fd`
   曾被污染(F-ECO 批3+4 同族 GOTCHA),从 vfs/ext2 syscall 单测查起。
2. **评估 `FDTable` 是否保留 fd 0/1/2**:`alloc` 跳过低号(除非显式开 stdio),对齐 Unix
   惯例 —— 这能根治整类"测试残留占 stdio 槽"污染。设计变更,propose-then-execute。

接 [f-usability-symlink-follow](../../document/ai/PLAN.md) / memory `f-usability-ci-busybox-followup`。
