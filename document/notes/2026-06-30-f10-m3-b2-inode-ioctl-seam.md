# F10-M3 Phase 2 批2 — InodeOps::ioctl 接口缝 + sys_ioctl fd>2 派发

> 里程碑:F10-M3 TTY Phase 2(PTY)。批2(头号风险批)。分支 `feat/f10-m3-pty`。commit `aadde89`。

## 背景

批1 的 PTY 是纯逻辑对象,没接 fd。要让一个进程拿到 PTY 的 master fd 并对它 ioctl(拿 termios、
挂控制终端等),需要两件基础设施:

1. 一个能从 fd 找到"它背后的 inode / 设备"并派发 ioctl 的路径——现状 `sys_ioctl` 把 `fd<=2`
   写死成 console,fd>2 直接 `-ENOTTY`,**没有任何 fd→设备 的 ioctl 派发**。
2. 设备 inode 能响应 ioctl——`InodeOps` 虚表里**没有 ioctl 方法**。

批2 就是把这两条缝补上(fd>2 走 inode 派发 + InodeOps::ioctl virtual),为 B3 的 PTY ops override 铺路。

## 目标 / 范围

- `InodeOps` 加 `ioctl(const Inode*, uint32_t request, uint64_t arg)` virtual,默认返 `NotImplemented`。
- `sys_ioctl`:fd≤2 走原 console 路径(**行为零变**);fd>2 走 `FDTable→File→Inode→ops->ioctl`。
- **行为零回归**:fd>2 既无 File、或 inode 用默认 ioctl,都仍返 `-ENOTTY`(保 `test_sys_ioctl_non_tty_fd_enotty`)。
- **`open()` virtual 推到 B3**:它第一个用户是 B3 的 `/dev/ptmx`(open 时分配 PTY 对),随用户一起
  落地免死代码。批2 只动 ioctl 缝。

## 关键决策

- **fd≤2 保持 console 特殊路径,不统一**。调查发现 `sys_read fd==0` 本就是特殊路径(`if (fd==0)
  console_tty().read()`),0/1/2 **不是真 FDTable 项**。若把 0/1/2 也改成真 `/dev/console` inode,
  得连带改 sys_read/sys_write 的特殊路径——爆炸半径大。低风险加法(用户拍板):fd≤2 不动,只给
  fd>2 加 inode 派发,与 read/write 对 fd>2 的 FDTable 派发一致。
- **`NotImplemented → -ENOTTY`(不是 ENOSYS)**。`to_errno(NotImplemented)=ENOSYS`,但 ioctl 的语义
  里"该 inode 不处理 ioctl"= ENOTTY(Linux:对普通文件做 tty ioctl 返 ENOTTY)。所以在 sys_ioctl 的
  fd>2 路径里特判 `r.error()==NotImplemented → -ENOTTY`,PTY ops 自己返的真错误(EFAULT/EINVAL)仍走
  `to_errno` 正常映射。Cinux-Base 的 Error 枚举不加新成员(子模块边界,不擅改)。
- **无 File 的 fd>2 也 `-ENOTTY`**(不是 -EBADF)。因为 `test_sys_ioctl_non_tty_fd_enotty` 对 fd 99
  (无 File)断言 `-ENOTTY`。保这个契约 = 零回归。-EBADF 更"正确"但会破测试,且语义上"非 tty fd"
  返 ENOTTY 也讲得通,留着。

## 实现

- [inode.hpp](../../kernel/fs/inode.hpp):`InodeOps` 加 `ioctl` virtual(默认 doc 注明 NotImplemented→ENOTTY)。
- [inode.cpp](../../kernel/fs/inode.cpp):默认 `InodeOps::ioctl` 返 `NotImplemented`(同其他默认方法范式)。
- [sys_ioctl.cpp](../../kernel/syscall/sys_ioctl.cpp):console switch 原样抽成 `console_ioctl()` helper;
  `sys_ioctl` 新增 fd>2 分支:`current_fd_table().get(fd)` → `file->inode->ops->ioctl(...)`,ok 返值,
  NotImplemented→-ENOTTY,其它错误 -to_errno,无 File→-ENOTTY。

## 验证

- host ctest:**65/65**
- run-kernel-test-all:**977/0 两 leg**(单核 + -smp 2 AP 回读 PASS)——**与批2 前基线完全一致**,
  即行为零回归。回归网:`test_sys_ioctl_non_tty_fd_enotty`(fd 99→-ENOTTY)、`test_sys_ioctl_unknown_
  cmd_enotty`(fd 1 未知 cmd→-ENOTTY)、console TCGETS/TCSETS/TIOCGPGRP/TIOCSPGRP 全过。
- 改公共头 `inode.hpp`,已全量编(`cmake --build build`)零新警告(显示的 frame-larger-than 等警告
  全在 test kernel 的 test_ahci/test_apic/test_bitmap_icon,基线既有,非本批引入)。

## 下一步

B3:DevFS PTY 节点——加 `InodeOps::open` virtual + 接进 sys_open;`/dev/ptmx` open 时分配一对 PTY、
返 master inode;`/dev/pts/N` lookup 命中 slave ops;master/slave InodeOps override read/write/ioctl
走 Pty。B3 会正面锻炼批2 铺的 fd>2 ioctl 派发。
