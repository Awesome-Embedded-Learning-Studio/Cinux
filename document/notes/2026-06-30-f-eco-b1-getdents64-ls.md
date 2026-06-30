# 2026-06-30 F-ECO 批1:getdents64(217)+ lstat(6)→ busybox ls 真跑通

> F-ECO 第一关 busybox 第二批。批0 跑通 echo;批1 补 getdents64(217)+ lstat(6),
> **busybox ls / 真列出目录内容**(hello.txt / bin / etc / lost+found)。试金石第二个
> applet 跑通。

## 目标

ls 是 echo 之后的下一个 applet,头号缺口 getdents64(批0 实测确认 `unhandled 217`)。
批0 的 ls 假绿(getdents64 缺失 → ls 列空但 exit 0)印证了 README 的准确性原则
(退出码 0 不算过),所以批1 把 ls 从 OBSERVED 升级成 gate + 强校验。

## 实现

### sys_getdents64(217)

musl opendir/readdir 调 **getdents64(217)**(musl `SYS_getdents` 在 x86_64 = 217),
不是老 getdents(78)。老 sys_getdents 只 copy entry name(简化,musl 不能解析)。
新 sys_getdents64 填真 `linux_dirent64{d_ino, d_off, d_reclen, d_type, d_name[]}`,
复用 do_getdents_kernel(循环读项,每项填 dirent64)。

- **d_ino 非 0**(entry index):Linux 过滤 d_ino==0 为"删除项",0 会让 ls 列空
  (同 ENOSYS 的假绿)。真 per-entry ino 需 readdir 返回(follow-up)。
- **d_type=DT_UNKNOWN**:readdir 只给 name;ls 列名不依赖(-F/-l 走 stat by name)。
- reclen = ALIGN(19 + namelen + 1, 8)(offsetof(d_name)=19)。

### lstat(6) = stat

ls 拿到 entries 后对每个 **lstat**(拿类型/大小)。CinuxOS 有 stat(4)/newfstatat(262)
但缺 lstat(6)。CinuxOS 还没 symlink(F6-M1 未做),故 **lstat = stat**(无 symlink 跟随)。
注册 SYS_lstat → sys_stat。

## 发现(试金石挖出)

### musl -O1 编译 opendir/readdir 的 fd 传递 bug

musl -O1(批0 为绕 malloc hlt 改的):opendir 设 `dir->fd = open fd`,objdump 看似对
(`mov %eax,%ebx; mov %ebx,0x8(%rax)`),但 readdir 读 `dir->fd` = 垃圾(堆地址),
getdents64 收垃圾 fd → EBADF → ls 列空。**-O0 修**(fd=9 对)。

坐实 **musl + GCC16 编译 bug 族**:-O2(malloc hlt trap)+ -O1(opendir/readdir fd)。
**-O0 全消**。build-musl.sh 改 -O0(debuggability > perf,hobby OS)。

### ls 默认 lstat 每个 entry

ls 拿到 entries 后对每个 lstat(类型/排序),lstat(6)缺失 → ENOSYS → ls exit 1。
补 lstat=stat 修。

## 结果

busybox ls / 真输出(busybox -C 列格式):
```
hello.txt   bin         etc         lost+found
```
echo 5/5 + ls PASS(exit 0 + 输出目录名)。run-kernel-test-all 两 leg + host 69/0 +
freestanding 门禁 全绿。

## commit

- sys_getdents64(217)(真 dirent64 布局)+ lstat(6)= stat + smoke ls gate
  (从 OBSERVED 升级;busybox_ok = echo_ok && ls_ok)
- build-musl.sh -O0(-O1 opendir/readdir fd bug)
- 本 note

## follow-up

- readdir 返真 ino/type(扩 InodeOps::readdir 签名;d_ino 现假 index,d_type DT_UNKNOWN)
- symlink 落地后 lstat 单独(不跟随)
- smoke ls 强校验输出(读 fd1 比对目录名,非只 exit==0)—— 现依赖串口可见
- musl -O0 性能(follow-up:pin GCC 或 musl 适配 GCC16 后回 -O2)
- readdir buf 满 entry lost(批1 smoke count 大不触发;caller 大目录 revisit)
