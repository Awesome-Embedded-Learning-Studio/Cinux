# F10-M3 Phase 2 批3 — DevFS PTY 节点(/dev/ptmx + /dev/pts/N + master/slave ops)

> 里程碑:F10-M3 TTY Phase 2(PTY)。批3。分支 `feat/f10-m3-pty`。commit `5e50572`。

## 背景

批1 的纯 `Pty` + 批2 的 `InodeOps::ioctl` / fd>2 派发,把"PTY 逻辑"和"ioctl 派发缝"备齐了。
批3 把两者拧在一起,让一个进程能走 Linux PTY ABI:

```
fd = open("/dev/ptmx")        // 克隆:分配一对 PTY,返 master inode 的 fd
ioctl(fd, TIOCGPTN, &n)       // 拿 pty 号
slave = open("/dev/pts/<n>")  // 拿 slave inode 的 fd(shell 端)
read(slave)/write(slave)      // shell 读 cooked 行 / 写输出
read(fd)/write(fd)            // 终端模拟器读 slave 输出 / 喂 slave 输入
ioctl(slave, TCGETS/TCSETS)   // 取/设 termios
```

## 目标

- `/dev/ptmx` 开 open 时克隆:分配一对 PTY,把 master inode 绑到新 fd。
- `/dev/pts/<N>` 解析到对应 slave inode。
- master/slave `InodeOps`(read/write/ioctl/stat)走 `Pty`。
- slave 支持 `TCGETS`/`TCSETS`/`TIOCGWINSZ`(经 `slave_tty()` + copy_to/from_user)。
- **`devfs.cpp` 保持 host 可测**:PTY 装配只在 kernel-only 的 `devfs_init.cpp`。

## 设计 / 决策

**1. 槽位注册表,不是动态分配**。`PtySlot g_slots[kMaxPtys]`(8 槽),每槽 inline 拥有
`Pty + master_inode + slave_inode`。`pty_alloc()` 找首个空槽,`wire_slot()` 把两个 inode 的
`ops` 指向共享的 `g_master_ops`/`g_slave_ops`、`fs_private = &slot.pty`(ops 据此找回 Pty)。
`pty_release()` 标记空槽给复用(测试清理 + 未来 close)。固定表 = 简单 + 确定性,Linux 动态分配留后。

**2. `InodeOps::open` 克隆钩子**。`InodeOps` 加 `open()` virtual(默认返原 inode);`sys_open` 在
lookup 后调 `inode->ops->open(inode)`,把返回的 inode 绑到 fd。`PtmxOps::open` override 成
`pty_alloc() + 返 master_inode`。普通文件 open() 走默认(零行为变)。这是 Linux `/dev/ptmx` 克隆范式。

**3. `Pty::reset()` 而非 move-赋值**。槽位复用时 `wire_slot` 调 `pty.reset()`。不能用 `pty = Pty{}`
(move 赋值):echo sink 的 `echo_ctx_` 会指向被销毁的临时 → 悬垂。`reset()` 在原位清 master 环 +
`slave_tty_ = TTY{}`(新行规范)+ **重新 `set_echo_sink(this)`** 把 echo 锚回当前对象。

**4. `/dev/pts/N` = dynamic lookup 钩子**。DevFs 固定节点表装不下按需分配的 slave。给 `DevFs` 加
`set_dynamic_lookup(resolver)`,`lookup()` 静态表未命中时回调。`devfs_init` 装的 resolver 解析
`pts/<N>` → `pty_slave_inode(N)`。DevFs 自身不认 PTY(保持 host 可测,层化干净)。

**5. inode 号编码 pty 索引**。`kPtyInoBase=0x1000`,master/slave inode 的 `ino = base + index`;
`TIOCGPTN` 返 `ino - base`。高基址避撞 DevFs 自己的 1..N 节点号。

**6. copy 失败 ≈ EINVAL**(非 EFAULT)。Cinux-Base 的 `Error` 枚举无 `Fault` 变量子模块边界不擅改;
slave ioctl 的 copy_to/from_user 失败返 `InvalidArgument`(→ EINVAL)。Linux 会返 EFAULT,但 libc
总传有效指针,这条只在故意坏指针时触发,记录为已知近似。

## 踩坑

- **`struct stat` 在 `namespace cinux::drivers` 里解析不到 `cinux::fs::stat`** → stat override 签名
  不匹配(`does not override`)。devfs.cpp 在 `cinux::fs` 内没这问题。修:`using cinux::fs::stat;`。
- **`TEST_ASSERT_TRUE(x)` = `TEST_ASSERT((x) == true)`**—— bitmask(`c_lflag & kIcanon` = 0x2)
  `== true(1)` 恒假!debug kprintf 才看出 termios 其实是对的(0x3b)。修:`(… & flag) != 0` 强制布尔。
- **copy_to/from_user 在 ring0 拒内核地址**(`access_ok` 卡 user range,同 `test_sys_ioctl_tiocspgrp_
  kernel_addr_efault`)。所以内核测**不能**验 TCGETS/TIOCGPTN 的 copy 路径(传 &local 被拒);改白盒验
  `slave->fs_private`→Pty termios 接线,termios-copy 留 B5 真 sys_ioctl 端到端。
- **槽位累积**:原测 9 alloc > kMaxPtys=8 → 表满。加 `pty_release()` 每测清理。

## 验证

内核测 `test_pty_device`(7 例):alloc/slave-lookup、master↔slave canonical round-trip、slave 输出→
master、本地回显→master、slave→Pty termios ICANON 接线(白盒)、未知 ioctl 拒绝、release 复用。
- host ctest:**65/65**(devfs.cpp 改动无回归)
- run-kernel-test-all:**984/0 两 leg**(977 基线 + 7 PTY 测)+ AP 回读 PASS

## 下一步

B4:控制终端语义——`TIOCSCTTY` 设 `Task::controlling_tty` + 挂 PTY(接 F3-M3 现成的 pgid/sid,
`setsid` 已留缝)、`/dev/tty` 别名当前控制终端、session 抢占语义 + 负测(非 leader→EPERM)。
