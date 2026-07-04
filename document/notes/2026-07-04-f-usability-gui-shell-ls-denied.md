# F-USABILITY:GUI shell `ls` permission denied 排查(sys_stat 不 follow symlink)

> 2026-07-04, 分支 `feat/f-usability-b4-gpp`。用户在 GUI shell(`run-single` +
> buildroot gcc rootfs)里敲 `ls` 报 `/bin/sh: ls: Permission denied`。排查
> 成一次方法论示范:**不猜测,加仪器看运行时**。

## 结论

根因是 `sys_stat` 用 `fs->lookup` 不 follow symlink。busybox `find_command`
用 `stat()` 判断 PATH 项是否 `S_ISREG`,CinuxOS stat 返回了 `/bin/ls` 这个
**symlink** 的类型(`S_IFLNK`)而非 follow 到 busybox(`S_IFREG`),shell 命令
查找失败 → 自己报 "permission denied"。**整条链没走到 access / execve**。

修复(commit `fff1fda`):`do_stat_kernel` 加 `follow` 参数改用 `vfs_lookup`;
`sys_stat` 默认 follow(对齐 POSIX);`sys_newfstatat` 认 `AT_SYMLINK_NOFOLLOW`;
新增 `sys_lstat`(NoFollow);kprintf 加 `%o`。顺带清了 `[COW] resolved` /
`[SCHED] blocked-unblocked` 调试 kprintf(commit `76054d0`)。

验证:`run-kernel-test-all` 两 leg **2318/0**、0 panic、调试 kprintf 残留 0;
GUI shell `ls` 闭环。

## 现象

GUI 模式(`desktop_launch` → 点 Shell 图标 → `create_shell_terminal` →
`TaskBuilder` spawn `/bin/sh`,走 PTY)下提示符正常,敲 `ls` 报:

```text
/bin/sh: ls: Permission denied
```

## 排查方法论(关键:不猜测)

### 初步推理(全是错方向)

凭直觉怀疑了几条,每条都被代码或实证否掉:

- **rootfs-gcc.ext2 文件 owner 1000**(`assemble_gcc_rootfs.sh` 没用 fakeroot,
  debugfs 确认 `/bin/busybox` 1000:1000)—— 但 grep 全 kernel 的 `EACCES` 出口 +
  读 `do_openat_kernel` / `execve`,发现 **VFS 路径完全不查权限位**,owner 对内核
  无影响。
- **`access(X_OK)` root 旁路** —— `access_granted` 算出来 root 该过(`0777 & 0111`)。
- **`execve` 返回 EACCES** —— `ExecveResult` 枚举里压根没 `-13`。

全是"算出来该过",没实证。用户叫停:**不猜测,看到底哪里触发**。

### 加仪器盖住所有 EACCES 出口

`grep -rn "kEacces\|PermissionDenied" kernel/` 只有两条出口:
`sys_access.cpp:101` 和 `pty_device.cpp`(PTY 抢终端,ls 不撞)。所以**如果**走
内核,EACCES 必从 sys_access 出。但也可能 busybox 自己根据 stat 结果报错(不调
access)。仪器盖两条路:

- `[DBG-STAT]` 在 `do_stat_kernel`:打 path / st_mode / perm / typebits / st_uid /
  st_gid / rc
- `[DBG-ACCESS]` 在 `do_access_kernel`:打 want / caller uid,gid / st_mode /
  st_uid,gid / OK|DENY

### kprintf `%o` 不支持 → 参数错位

第一次跑,`[DBG-STAT]` 输出:

```text
st_mode=0%o perm=0%o typebits=%o st_uid=41471 st_gid=511 rc=10
```

`%o` 原样输出 —— `vkprintf_impl.hpp` 的 `default` 分支输出 `%`+type **但不消费
`va_arg`**,后面 `%u`/`%ld` 全错位。倒过来解:

- `st_uid=41471` 实际是 st_mode(`0120777` octal = 41471 dec)
- `st_gid=511` 实际是 perm(`0777` = 511)
- `rc=10` 实际是 typebits(`0xA` = `S_IFLNK`)

用户明确要求**正经加 `%o` 支持,不 workaround**。`vkprintf_impl.hpp` 仿
`format_hex` 加 `format_octal` + `case 'o'`(查表 `digits[value & 0x7]` 避 narrowing)。

### 重跑 → 根因坐实

```text
[DBG-STAT] '/bin/ls' st_mode=0120777 perm=0777 typebits=12 st_uid=0 st_gid=0 rc=0
```

- `st_mode=0120777` = **S_IFLNK**(symlink 类型位 012)
- **没有 `[DBG-ACCESS]`,没有 `[EXECVE] loading '/bin/ls'`**

busybox `find_command` 用 `stat()` 找 `S_ISREG` 的可执行文件。CinuxOS
`sys_stat` 返回 symlink 自身(`S_IFLNK`)而非 follow 到 busybox(`S_IFREG`)
→ shell 不认 → 报 permission denied。**根本没走到 access 和 execve**。

## 根因

`sys_stat` 用 `fs->lookup(rel)`(注释:"only walks directory components and
never follows")。但 POSIX `stat(2)` **要 follow** symlink(lstat 才不 follow)。
`do_openat_kernel` / `execve` 都用 `vfs_lookup(Follow)`(正确),唯独 stat 系列
用 `fs->lookup`。`SYS_lstat` 还直接注册成 `sys_stat`(注释 "no symlinks yet")——
stat / lstat 完全没区分。

## 修复(`fff1fda`)

- `do_stat_kernel` 加 `bool follow` 参数,改用 `vfs_lookup(Follow | NoFollow)`;
  `sys_stat` 默认 follow
- `sys_newfstatat` 认 `AT_SYMLINK_NOFOLLOW`(0x100)—— **这是 musl lstat 的真路径**
  (musl 的 stat/fstat/lstat 都走 newfstatat)
- 新增 `sys_lstat` handler(NoFollow),`SYS_lstat` 不再注册成 `sys_stat`
- kprintf 加 `%o`(`vkprintf_impl.hpp` `format_octal` + `case 'o'`)
- 删排查用的 `[DBG-STAT]` / `[DBG-ACCESS]` 临时仪器

## 顺手清的调试 kprintf(`76054d0`)

排查时发现同类"成功路径无条件 kprintf"(跟 demand kprintf `75c399a` 一个病):

- `[COW] resolved fault`(`process_new.cpp`):每次 CoW 缺页打,fork 后写时复制刷屏
- `[SCHED] Task blocked/unblocked`(`scheduler_block.cpp`):PTY shell 每次按键
  shell block/unblock 各一条(用户敲的字符夹在日志中间)

`block()` 的 `reason` 参数保留(测试传诊断 tag),加 `(void)reason` 抑制 unused。

## 验证

- `run-kernel-test-all` 两 leg(单核 + `-smp 2`) **2318 PASS / 0 FAIL / 0 panic**
- 调试 kprintf 残留全 0(demand-read / [COW] resolved / [DBG-*] / [SCHED] blocked)
- GUI shell `ls` 闭环(permission denied 消失)

## follow-up(登记,留后续)

排查时 agent 扫出一整套**同族 symlink-follow 不对齐**(都该用 `vfs_lookup(Follow)`
却用 `fs->lookup`):

- `sys_access` / `sys_chmod` / `sys_chown` / `sys_utimensat` / `sys_link` /
  `sys_chdir`
- `sys_readlink` 用 `fs->lookup` 是**正确**的(readlink 不 follow)

其他:

- **`st_uid=0` vs debugfs 显示 1000** —— ext2 读 symlink inode 的 uid 读成 0
  (独立 bug,跟 ls denied 无关 —— `S_IFLNK` 才是病根;但 stat follow 后该字段
  仍可能错,留意)
- `[EXECVE]` / `[PROC] fork` / `[SYSCALL] exit` 成功路径 kprintf(shell 每命令
  刷 7+ 条,留噪声治理)
- ramdisk / procfs / devfs 部分 `InodeOps` 漏 override `stat`(返 ENOSYS;生产
  rootfs 是 ext2 不撞)
- `errno.hpp` `to_errno` 未映射 Error fallback `kEio`(安全网,可能掩盖)

## 教训

1. **不猜测**:推理出"该过"不算数,加仪器看运行时实际值。这次 `[DBG-STAT]` 一行
   就把 `S_IFLNK` 暴露了,省掉所有 owner/access 的瞎猜。
2. **kprintf `default` 不消费 `va_arg` 是隐患**:任何未知 `%` 让后续参数错位。加 `%o`
   缓解;但 default 行为本身该警惕(未知格式符该消费对应参数或报错)。
3. **POSIX 语义对齐**:stat/lstat/open/execve 对 symlink 的 follow 是 Linux ABI 契约,
   CinuxOS 不能自创。`fs->lookup` 是内部原语,syscall 边界该用 `vfs_lookup` + 正确
   flag。

接批4a([2026-07-04-f-usability-b4a-gpp-cpp](2026-07-04-f-usability-b4a-gpp-cpp.md))。
GUI shell ls 是批4a 闭环的图形化体验版,顺带暴露了 sys_stat 的旧债。
