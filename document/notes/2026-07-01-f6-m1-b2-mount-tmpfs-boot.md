# F6-M1 批2:sys_mount/umount2 + boot 自动挂 /tmp(GCC 自举主线批2)

> 2026-07-01。分支 `feat/f12-gcc-selfhost`,commit `b078fef`,**未 push**(接批1 tmpfs `6656096` + shm merge `2a86b13`)。
> GCC 自举第二刀:给内核**运行时挂载能力**(sys_mount/umount2)+ **boot 自动挂 /tmp**——GCC/cc1/as/ld 写中间 `*.o`/`*.s` 的地方。批1 的 TmpFs 此刻真正在生产 boot 装配成 `/tmp`。

## 实现

- **sys_mount(165)/umount2(166)**([sys_mount](../../kernel/syscall/sys_mount.cpp) / [sys_umount2](../../kernel/syscall/sys_umount2.cpp)):
  - **fstype 工厂**:`do_mount_kernel(source, target, fstype, flags)` — `strcmp(fstype,"tmpfs")==0` → `new TmpFs()` + `mount()` + `vfs_mount_add(target, fs, owned=true)`;其他 fstype → `-ENODEV`(errno.hpp 新增 `kEnodev=19`)。`source`/`flags`/`data` 接受但忽略(tmpfs 无后端设备、MS_* 未建模)。
  - **do_ 内核变体**(ring0 测直驱):`do_mount_kernel`/`do_umount2_kernel` 取已解析字符串;`sys_mount`/`sys_umount2` 包 SMAP user-ptr 读(`resolve_user_path` target、`read_user_path` fstype)。同 F-ECO GOTCHA:ring0 测不能走 syscall 的 copy_from_user 路径(`is_user_vaddr` 拒内核地址),故测直驱 do_。
  - 注册:[syscall_nums.hpp](../../kernel/syscall/syscall_nums.hpp) +SYS_mount=165/umount2=166;[syscall.cpp](../../kernel/arch/x86_64/syscall.cpp) include + `syscall_register`。
- **挂载表 ownership 模型**([vfs_mount.hpp](../../kernel/fs/vfs_mount.hpp) / [vfs_mount.cpp](../../kernel/fs/vfs_mount.cpp)):
  - `MountPoint` +`bool owned{false}` 字段;`vfs_mount_add(path, fs, owned=false)` 加默认参(**现有 2-arg 调用零改**)。
  - `vfs_mount_remove` 变 **ownership-aware**:`owned==true`(sys_mount 堆 FS)→ `delete fs` 再让槽;`owned==false`(boot 静态 / 测 mock)→ 仅让槽不释放(静态对象命超表)。
  - **关键安全性**:现有 13 处 `vfs_mount_remove` 调用(全 ext2/ramdisk/vfs_syscall 测 + host test_vfs_mount)都用默认 `owned=false`,**零行为变**(remove 不会误 free 它们的栈/mock FS)。sys_umount2 是唯一 owned=true 路径。
- **boot /tmp 接线**([tmpfs_init.cpp](../../kernel/fs/tmpfs/tmpfs_init.cpp) + [init.cpp](../../kernel/proc/init.cpp)):
  - `tmpfs::init()`(§14 file gate,镜像 devfs_init/procfs_init):静态 `g_tmpfs` + `mount()` + `vfs_mount_add("/tmp", &g_tmpfs)`(**unowned**)。
  - `kernel_init_thread` 在 `procfs::init()` 后调 `tmpfs::init()`。`/tmp` 静态挂载,`sys_umount2("/tmp")` 会摘槽但不释放(静态对象)。

## 机制测试(防假绿,5 测,直驱 do_mount/umount2_kernel)

[test_mount.cpp](../../kernel/test/test_mount.cpp):每测挂唯一 `/tmp_t_*` 路径 + **结尾必 umount**(全局挂载表跨 section 共享,遗留会污染后续测,同 fd 表 GOTCHA)。

- mount tmpfs → `vfs_resolve` 命中 → umount → resolve 返 null(摘槽)。
- mount → 经挂载的 FS `lookup("")`→`create`→`write`→`read` 逐字节精确 + 路径 `lookup("gcc_out.o")` 命中同一 inode(整条 mount→FS→ops 链)。
- 未知 fstype(`ext2`/`nonsense`)→ `-ENODEV`,挂载表无残留。
- umount 不存在路径 → `-ENOENT`;空/null target → `-EINVAL`。
- **mount/umount/remount**:挂 + 留 "stale" 文件 → umount(owned FS 真释放,树随之 free)→ 重挂同路径 → 新空 FS,"stale" 不见(验 owned backend 真被 delete,非泄漏进新实例)。

## GOTCHA

- **ownership-aware remove 的零行为变保证**:`owned` 默认 false → 所有旧调用者(boot 静态挂载 + 测的栈/mock FS)remove 时不会触发 delete。唯有 sys_mount 路径 `owned=true`。若误把 boot 静态挂载标 owned=true → umount 时 delete 静态对象 → 双重释放/崩溃。`tmpfs::init` / `devfs::init` / `procfs::init` 全用默认 2-arg(owned=false),安全。
- **ring0 测不走 sys_ user-ptr 路径**:同 F-ECO/AF_UNIX GOTCHA——`sys_mount` 的 `read_user_path(fstype_virt)` 经 `is_user_vaddr`,测试内核栈地址会被拒 → -EFAULT。故机制测直驱 `do_mount_kernel(nullptr, path, "tmpfs", 0)`。
- **/tmp 在测试内核不挂**:`run-kernel-test` 跑 `big_kernel_test`(test harness),不走生产 `init.cpp` → 测试内核无 /tmp。机制测自挂 `/tmp_t_*` 验;生产 /tmp 由 `make run` boot 冒烟验(`[TMPFS] mounted at /tmp`)。

## follow-up(留批3 / 后续)

- **MS_* flags**:现忽略(MS_NOSUID/MS_NODEV/MS_NOEXEC 等);busybox `mount -o` 选项解析留后续。
- **source-bearing fstype**:ext2/块设备挂载(需 source → 块设备 → Ext2);现仅 tmpfs(无源)。
- **MNT_FORCE**:umount2 flags 忽略;强卸(忙挂载)留后续。
- **/etc/mtab / /proc/mounts**:busybox `mount`(无参)列挂载点需 `/proc/mounts`;ProcFS 动态节点扩展留后续。

## 验证

- `cmake -B build`(TESTS=ON, BUSYBOX_SMOKE=ON)+ `cmake --build build -j` 编绿(kallsyms 16181,+199)。
- **`make run` 生产 boot 冒烟**:`[DEVFS] mounted at /dev` → `[PROCFS] mounted at /proc` → **`[TMPFS] mounted at /tmp`**,零 panic(/tmp 装配成功)。
- **`run-kernel-test-all` 两 leg 绿**:mount 5×2=10 + tmpfs 9×2 + shm 6×2 PASS,busybox 14/14 两 leg,**零 FAIL**,零回归。post-clang-format 单核复测 1081 passed / 0 failed。
- ⭐ **VNC 端口避让**:本机多 AI 会话共用 `-vnc :0`,验证临时切 `-vnc :5`,跑完还原(非提交,见 memory `verify-vnc-port-collision-multi-session`)。

**push/PR 归用户**——GCC 自举主线,主 checkout 串行 commit。
