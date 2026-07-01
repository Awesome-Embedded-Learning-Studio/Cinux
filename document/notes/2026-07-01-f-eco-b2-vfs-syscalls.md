# F-ECO 批2:VFS metadata + dirent 8 syscall

> 2026-07-01。分支 `feat/f-eco-b2-vfs-syscalls`(从 main `470e9c8`)。F-ECO busybox 试金石第二刀
> ([todo f-eco/00-busybox-touchstone.md](../todo/f-eco/00-busybox-touchstone.md) 批2):为 `cp/mv/rm/touch/ln/mkdir/chmod/chown`
> 普及 8 个 Linux syscall —— `rename`(82)/`symlink`(88)/`link`(86)/`readlink`(89)/
> `chmod`(90)/`chown`(92)/`umask`(95)/`utimensat`(312)。
>
> **打法(用户决策:严密分身并行)**:阶段0 主控独占铺公共契约,阶段1 两分身填 ext2 方法体。
> 对治用户教训"prompt 不全→分身改乱":先读全部签名模式(sys_unlink/sys_stat/ext2_directory/create-mkdir-unlink/inode.cpp/process.hpp/path_util)再铺契约;分身 prompt 注入完整契约 + Ext2 原语清单 + 严格单文件边界 + 禁碰公共头 + 不跑 cmake(主控统一编译)。两分身文件零交叉(ext2_common.cpp vs ext2_directory.cpp),零合并冲突。

## 架构(走 InodeOps 虚派发,对齐 sys_unlink/sys_stat)

- **setattr 组(chmod/chown/utimensat)+ readlink**:走 **lookup-target**(照 [sys_stat.cpp](../../kernel/syscall/sys_stat.cpp))——`vfs_resolve → fs->lookup(rel_path) → target->ops->chmod(target, ...)`。
- **dirent 组(symlink/link/rename)**:走 **lookup-parent**(照 [sys_unlink.cpp](../../kernel/syscall/sys_unlink.cpp))——`split_pathame → fs->lookup(parent) → parent->ops->symlink/link/rename`。
- **umask**:纯 proc(`Scheduler::current()->umask`),不走 VFS。
- 加 7 个 `InodeOps` **default virtual**(返 NotImplemented)——零 blast radius,~20 个子类(pipe/pty/devfs/procfs/ramdisk/socket)不动,仅 ext2 override(对齐既有 ioctl/open 扩展点模式)。
- `EXT2_S_IFLNK=0xA000` 常量;`Task::umask{022}` 字段。

## 批表

| 批 | 范围 | Commit | 测试 |
|----|------|--------|------|
| 阶段0 契约 | InodeOps 7 virtual + Ext2 方法/override 声明 + ext2 NotImplemented 桩 + 8 完整 sys handler(lookup→派发)+ syscall 号/注册/CMake | `11592ef` | 两 leg 1027/0 零回归 |
| 块A(分身) | ext2_common.cpp:setattr+readlink 实现 | `8a22e58` | 编绿 |
| 块B(分身) | ext2_directory.cpp:symlink/link/rename 实现 | `b653c14` | 编绿 |
| fix + 测 | cache 一致性 + 符号链接 ops + 6 机制测试 | `9bcd037` | 两 leg 1033/0(+6) |

## GOTCHA(机制测试抓的两类真 bug——防假绿纪律生效)

1. **stat 读 inode cache,不读盘**:[Ext2FileOps::stat](../../kernel/fs/ext2_common.cpp) 读 `cached->disk_inode`(内存)。`Ext2::chmod/chown/utimensat/link` 写盘后若不失效 cache → stat 读旧值 → 假失败。加 `Ext2::invalidate_cached_inode(ino)` private helper,各 setattr 成功路径调用。
2. **populate_vfs_inode 不认 S_IFLNK**:[ext2_inode.cpp](../../kernel/fs/ext2_inode.cpp) 只 if S_IFDIR/else if S_IFREG,符号链接走 else → `ops=nullptr` → readlink 拿 nullptr → -EIO。S_IFLNK 复用 `file_ops_`(readlink 读数据块同文件 read 机制)。

> **教训留存**:分身按 prompt 正确填方法体,但 cache 一致性是**跨层隐式契约**(setattr 写盘 vs stat 读 cache),prompt 没显式说"写盘后失效 cache"——分身只看 ext2 方法,看不见 stat 怎么读,不可能自己发现。**机制测试是唯一防线**。这正是"绿≠对":分身编绿 + 基线零回归,但 stat 假失败。F-ECO 试金石"退出码 0 不算过,语义精确匹配才算过"的价值在此。

## follow-up(留后续)

- **busybox -O2 实测**(cp/mv/ln/touch/chmod/chown 真跑):本地无 busybox build(`build/musl/busybox` 不存在),留 CI 或本地建 busybox 后验。
- **rename overwrite**:dst 已存在直接返 false(不覆盖,Linux rename 会替换)。
- **rename 目录调 parent nlink**:Linux rename 目录会调源/目 parent nlink,hobby-OS 简化不做。
- **umask 应用到 create/mkdir**:现只 get/set Task::umask 字段,ext2 create 硬编码 0644/mkdir 0755 不读 umask。
- **symlink follow**:lookup 不 follow(无符号链接解析),`cat symlink` 读 target 内容未实现(读的是符号链接数据块=target 字符串)。
- **chown rev0 inode 16 位**:丢 uid/gid 高 16 位。
- **utimensat nsec 截断**(rev0 只秒);`times=NULL` 用 wall clock(现 0 占位,需接 RTC/HPET)。

## commit 链

`470e9c8` → `11592ef` → `8a22e58` → `b653c14` → `9bcd037`。push/PR 归用户。
