# F10-M3 TTY Phase 2(PTY)— 收官

> 里程碑:F10-M3 TTY Phase 2。分支 `feat/f10-m3-pty`(6 批 B0-B5)。本篇是收官总结。
> commit 链:`26bb206`(B0 立项)→ `bab9caf`(B1)→ `aadde89`(B2)→ `5e50572`(B3)→ `6e66217`(B4)。

## 做了什么

把 Phase 1 的"console TTY 单例"升级成 Linux 风格**多路 PTY**——任意进程能在伪终端下跑(真终端模拟器/
GUI shell 驱独立 shell 进程的地基,F10-M4 CFBox 的前置)。

| 批 | 内容 | commit |
|----|------|--------|
| B0 | 立项 docs + 顺手清 PR#45-49 文档债(5 个已合里程碑的「待 PR/🔄」改对) | `26bb206` |
| B1 | `pty.{hpp,cpp}` 纯逻辑(master/slave 双 ring + slave 复用 `TTY` 行规范)+ host 单测 10 例 | `bab9caf` |
| B2 | `InodeOps::ioctl` virtual + `sys_ioctl` fd>2 走 `fd→File→Inode→ops->ioctl` 派发(fd≤2 console fallback 零回归) | `aadde89` |
| B3 | `InodeOps::open` 克隆钩子 + `sys_open` 接线 + `pty_device` 注册表 + master/slave/ptmx ops + DevFS `/dev/ptmx` + `/dev/pts/N` 动态节点 + 内核测 7 例 | `5e50572` |
| B4 | `TIOCSCTTY`(session leader 挂控制终端)+ `/dev/tty` 每进程别名 + 内核测 2 例 | `6e66217` |
| B5 | `make run` boot 冒烟(PTY DevFs 装配零 panic)+ 收官 | 本篇 |

## 验证

- **host ctest 65/65**(含 B1 的 pty 10 例)
- **run-kernel-test-all 986/0 两 leg**(977 基线 + 9 PTY 设备/控制终端测)+ AP 回读 PASS
- **`make run` boot 冒烟**:`[DEVFS] mounted at /dev (4 nodes)`(null/zero/console/**ptmx**),零 panic ——
  生产 boot 真加载 `/dev/ptmx` 节点 + `/dev/tty` + `/dev/pts/N` resolver。
- 改公共头 `inode.hpp`(加 `ioctl`/`open` virtual)push 前已全量编,零新警告。

## 关键设计

- **slave 复用 Phase 1 `TTY` 行规范**不重写;echo 经 TTY echo sink 回 master 读侧(本地回显经对)。
- **`InodeOps::ioctl`/`open` 两个 virtual** 对齐 Linux fops(`unlocked_ioctl`/`open` 克隆)。默认 `ioctl`→
  `NotImplemented`→`-ENOTTY`;默认 `open`→原 inode。fd≤2 保 console 特殊路径(read/write/ioctl 一致),
  fd>2 走 FDTable→File→Inode→ops 派发。
- **PTY 注册表固定 8 槽**(`PtySlot g_slots[8]`,inline 拥有 Pty + master/slave Inode);`fs_private=&slot.pty`
  让共享 ops 找回 Pty;`ino` 高基址编码 pty 索引。
- **`/dev/ptmx` 克隆**:`PtmxOps::open` 分配一对、返 master inode。**`/dev/pts/N` + `/dev/tty`** 经 DevFs
  `set_dynamic_lookup` 回调(devfs_init 装,devfs.cpp 不碰 PTY 保 host 可测)。
- **`Pty::reset()`** 原位清状态 + 重锚 echo sink(move 赋值会让 echo_ctx 悬垂)。

## follow-up(诚实登记)

1. **fork+execve-under-PTY 全闭环 → F10-M4**(本次明确推迟,用户拍板):真用户程序跑在 PTY 里需要
   `sys_dup2`(把 slave 重定向到子进程 stdio,**当前未实现**)+ 真 session + 一个会 `open("/dev/ptmx")` 的
   shell。F10-M4 CFBox 是天然消费者,它本就需要 dup2。本次没合成 ring3 PTY smoke——免抢 F10-M4 的活。
   另:ring0 harness 不能调带 user 指针的 syscall(`access_ok` 拒内核址),所以纯 ring0 syscall smoke 也做不了。
2. **blocking-on-empty slave read**:现状 slave read 非阻塞(0 = 无数据/EOF 经 take_eof 分)。真 shell 读
   stdin 要阻塞(像 console_tty 那样睡等行),需 reader Task + 唤醒机制,留 follow-up。
3. **PTY close→pty_release 接线**:close slave/master fd 时应 `pty_release(index)` 释放槽位。当前无 close
   钩子(槽位靠测试显式 release)。生产用要补 close 路径(可能经 `InodeOps::release` 或 fd close 回调)。
4. **errno 近似**(Cinux-Base Error 枚举所限,子模块边界不擅改):copy 失败→`EINVAL`(Linux EFAULT);
   TIOCSCTTY 拒绝→`EACCES`(Linux EPERM)。行为对,errno 近似,记录。
5. **PTY 单实例限 8 对**(kMaxPtys);动态扩展留后。

## 踩坑(详见各批 note)

- B1:`add_cinux_test(pty ...)` 的可执行 target 名是 `test_pty`(ctest 名才是 `pty`);`[[nodiscard]] ErrorOr`
  返回值忽略触发 `-Wunused-result`(零警告纪律),改 `CHECK(... .ok())`。
- B2:`to_errno(NotImplemented)=ENOSYS` 但 ioctl 语义要 ENOTTY,sys_ioctl 特判 `NotImplemented→-ENOTTY`;
  `test_sys_ioctl_non_tty_fd_enotty`(fd 99)是回归网。
- B3:`struct stat` 在 `namespace cinux::drivers` 解析不到 `cinux::fs::stat` → stat override 签名不匹配,
  `using cinux::fs::stat;` 修;`TEST_ASSERT_TRUE(x)` = `((x)==true)`,bitmask(`& kIcanon`=0x2)`==1` 恒假,
  要 `!= 0` 强制布尔;copy_to/from_user 在 ring0 拒内核址 → termios-copy ioctl 只能白盒测/留 B5。
- B4:TIOCSCTTY 的 steal 标志内联在 ioctl 字(无 user 指针),ring0 可测;用 `Scheduler::set_current(&stack_task)`
  造 session-leader / 非-leader current(mirrors test_clone/test_brk)。

## 下一步

F10-M3 TTY 全里程碑(Phase 1 + Phase 2)收官。F10 域下一焦点:F10-M4 CFBox+init(真用户态 init/shell,
PTY 的天然消费者,会驱动实现 dup2 + 把 shell 放进 PTY),或转 F6/F7 其它 FS/网络里程碑。
