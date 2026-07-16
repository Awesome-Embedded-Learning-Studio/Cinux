# /dev/tty 不进 DentryCache(每进程动态解析)

> 日期：2026-07-12 · commit `075ef63` · v1.0.0 发版后真实可用化 · 状态：✅ 合 main

## 现象

录 v1.0.0 多终端视频时：开 shell 0（PTY 0）后再开 shell 1（PTY 1），shell 1 **没 prompt、不能退格、
无法交互**——busybox sh 的 job control 用 `tcgetpgrp(/dev/tty)` 比对自己 `getpgrp()`，不匹配就**自发
SIGTTIN** 停住。shell 0 正常，shell 1 串到了 shell 0 的 PTY。

## 根因

`/dev/tty` 解析为**调用者当前 controlling_tty 对应的 PTY slave**（`devfs_init.cpp` 的
`devfs_resolve`：`pty_slave_inode(task->controlling_tty)`）——每进程不同。但 `vfs_lookup` 的
`DentryCache` 缓存 `(parent, name) -> child Inode`，DevFs 第一次解析 `/dev/tty` 后缓存了结果，后续所有
进程开 `/dev/tty` 都命中缓存拿到**第一个进程的 PTY**。

- shell 0：`/dev/tty` → PTY 0（fg=3=自己 pgrp，匹配）✓
- shell 1：`/dev/tty` 命中缓存还是 PTY 0（fg=3 ≠ 自己 pgrp 4）→ SIGTTIN → Stopped ✗

`/dev/pts/N` 没事：每个 N 是不同 name → 不同缓存键 → 各自解析。

## 定位

在 `devfs_resolve` 加日志 `[tty] /dev/tty -> pty_slave(N) (caller pid=X ctty=Y)`，复现发现 **shell 1 没打
这行**（没走解析）但 TIOCGPGRP 查到 PTY 0 → 说明走了缓存没调 resolver。

## 修复

`FileSystem` 加 `virtual bool dcache_enabled() const { return true; }`；DevFs 覆盖 `return false`（它本来
在内存里 lookup 便宜，缓存反而出错）；`vfs_lookup` 的 `DentryCache::add` 加 `if (fs->dcache_enabled())`
守卫。ext2/tmpfs 照常缓存。

## 教训

- **动态 / 每进程的 lookup 不能进 dentry cache**。Linux 的 `/dev/tty` 是特殊 char device，open() 时才
  redirect 到 controlling tty（节点本身静态可缓存）；Cinux 用 devfs 动态 lookup 实现，得显式跳过缓存。
- 加 `setsid` + `TIOCSCTTY` 让 busybox sh 开 job control 后，会暴露一堆之前没触发的 jc 路径（SIGTTIN /
  SIGTSTP / tcgetpgrp）。与 ping Ctrl+C 弧同源，见
  [2026-07-12-net-ping-ctrl-c-usability](2026-07-12-net-ping-ctrl-c-usability.md)。

## 验证

两 leg run-kernel-test-all 绿；多终端每个 shell 各自的 `/dev/tty` 独立解析，job control 正常。
