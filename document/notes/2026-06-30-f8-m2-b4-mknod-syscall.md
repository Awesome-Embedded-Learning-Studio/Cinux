# F8-M2 批4 — sys_mknod / mkfifo + DevFS FIFO 命名节点

> 2026-06-30，worktree `worktree-f8-pipe-fifo`。F8-M2 第四批。

## 背景 / 目标

把批3 的 FIFO 机制接到 syscall + VFS：`mkfifo(path, mode)` = `mknod(path, S_IFIFO|mode, 0)`；
open 该名字经 DevFS 动态解析命中 FIFO inode → FifoOps::open cloning。给 FIFO 一个可 open 的名字。

## 设计 / 决策

**SYS_mknod = 133**（Linux x86_64 号，新注册）。`do_mknod_kernel(path, mode)`：
- `(mode & 0xF000) != kSIfFifo` → `-ENOSYS`（本里程碑只做 FIFO，char/block 留后）。
- 取路径 leaf（最后一个 `/` 之后，是 resolved path 的 NUL 结尾尾段）→ `FifoRegistry::create(leaf)`。
- **flat 命名空间**：FIFO 按 leaf 名注册，DevFS 动态查找解析（hobby-OS 简化：FIFO 落 /dev，
  不做任意路径；同 leaf 不同目录会撞名——可接受）。

**FifoRegistry 扩展**：Entry 持稳定 `Inode inode`（create 时设 ops=`fifo_ops()`、
fs_private=`&entry.fifo`、mode=`kSIfFifo|0666`、ino=`kFifoInoBase+i`）；新增 `lookup_inode(name)`
供 DevFS 解析返稳定 inode（不 leak）。

**DevFS 动态查找扩 FIFO**：`devfs_init.cpp` 把 `pty_dynamic_lookup` 改名 `devfs_dynamic_lookup`，
**先查 FifoRegistry**（命中返 FIFO inode；NotFound 落到 PTY /dev/tty、/dev/pts/N；其它错传播）。
FIFO 名先查 → 不与 PTY 撞。

## 陷阱

**`pty_dynamic_lookup` 改名**：函数名变了，`set_dynamic_lookup(&pty_dynamic_lookup)` 那行也要同步改，
否则 undeclared identifier（IDE 即时抓到）。重命名要全 callers 同步。

**fifo.cpp 在 `namespace cinux::ipc`**：`Inode`/`InodeType`/`InodeOps` 都要 `cinux::fs::` 全限定
（同批3 的 Spinlock/stat 教训）。

## 验证

- `run-kernel-test-all` 两 leg **990/0**，0 panic / 0 #DF。**关键**：DevFS dynamic_lookup 改动进
  了每条 /dev 查找的热路径——两 leg 全绿证 PTY/console/`/dev/tty` 解析零回归。
- host test_fifo 仍绿（fifo.cpp 加了 lookup_inode + Entry.inode，host 路径不涉）。
- 跨 fd mkfifo→open→write/read round-trip 真测在批5（本批只接 syscall + DevFS）。

## 范围栅栏（诚实）

- 只支持 S_IFIFO；mknod 其它节点类型 -ENOSYS。
- flat 命名空间（leaf 名），FIFO 落 /dev；任意路径 mkfifo + open 闭环留真 shell。
- sys_mknod 经 do_mknod_kernel（内核内部可调）；用户态经 SYS_mknod trap。

## 下一批

批5（收官）：跨 fd mkfifo→open 写端→open 读端→write/read round-trip→close 真测 + 两 leg + ROADMAP F8 ✅。
