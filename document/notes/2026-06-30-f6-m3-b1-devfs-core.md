# F6-M3 批1:DevFS 核心 — device inode 子类 + null/zero/console

**日期**:2026-06-30
**里程碑**:F6-M3 DevFS(F6 VFS 第三里程碑,三路并行之一:另两路 F7-M4 UDP / F10-M2 动态链接)
**分支**:`worktree-f6-m3-devfs`(worktree 从 main `1cdd507`)
**commit**:`bb7310e`

## 背景

F10-M3 TTY Phase2 的 PTY / `/dev/*` 依赖 DevFS;三路并行需共享 [inode.hpp](../../kernel/fs/inode.hpp) 的 `InodeOps` 虚表(F10-M2 动态链接在用)。DevFS = 内存型虚拟 FS(无 ext2 后端):device inode(`InodeOps` 子类,封装设备驱动 ops)→ `/dev` 虚拟挂载 → 基础节点(`/dev/null` `/dev/zero` `/dev/console`)。对齐 Linux DevFS/tmpfs 的 device-inode 模式。

## 范围栅栏(并行硬边界)

**不改 `InodeOps` 虚函数接口**(签名一行不动),只加子类;`st_rdev` 收进 ops 子类 `stat()` override,不加 `Inode` 字段。不做 mknod / 块设备 / ProcFS / tmpfs / ext4(留 F6 后续)。`/dev/console` write→serial(基础节点),接 ConsoleTty 真 stdin / PTY 留 F10-M3 TTY Phase2。

## 做了什么

1. **[devfs.hpp](../../kernel/fs/devfs.hpp) / [devfs.cpp](../../kernel/fs/devfs.cpp)(新)**:
   - `DevFs : FileSystem`:内存设备表(`DevNode{name, Inode}` × `DEVFS_MAX_NODES=16`),`mount()` 建标准节点,`lookup()` 按名查(去前导 `/`,空/`/`→根目录 inode)。
   - 设备行为 = 匿名 namespace `InodeOps` 子类:`NullDevOps`(1:3 丢弃/EOF)、`ZeroDevOps`(1:5 读零/丢弃)、`ConsoleDevOps`(5:1 write→注入 `CharSink`)、`DevDirOps`(readdir 遍历节点表)。
   - `CharSink` 抽象(`write(buf,count)→ErrorOr<int64_t>`):console 写槽,kernel 注入真 sink,host 注入 mock → 派发逻辑 host 可测。
   - 设备号 `devfs_makedev(major,minor) = (major<<8)|minor` 收进各 ops 子类;`stat()` override 填 `st_rdev` / `st_mode`(`kSIfChr|0666`)。
2. **双路单测**:host [test_devfs.cpp](../../test/unit/test_devfs.cpp)(19 例,link 真 devfs.cpp + mock sink)+ kernel [test_devfs.cpp](../../kernel/test/test_devfs.cpp)(7 例 `run_devfs_tests`)。
3. CMake:devfs.cpp 进 `big_kernel_common`;host 集成测 + `ALL_HOST_TESTS`;`main_test.cpp` 派发。

## 设计要点

- **类化非 C 风格**:DevFs + 设备 ops 子类 + CharSink(memory `classify-c-style-singleton-with-mutable-state`),非全局 static + 自由函数。对齐 ramdisk 范式(FileSystem 子类 + 匿名 namespace ops + `fs_private` 回指)。
- **CharSink 解耦**:console 设备不直接依赖 Serial(host 不可链);devfs.cpp 零 kprintf、host 可链(§14 文件 gate:devfs_init.cpp kernel-only 单独编译,批2)。
- **RAII**:`~DevFs()` delete ops(避免 host ASAN 报 leak,比 ramdisk 更干净);`mount()` 幂等守卫(`node_count_>0` no-op)。
- **设备号收进 ops**:`st_rdev` 从 ops 子类 `stat()` override 填,不碰 `Inode` 字段(并行栅栏)。

## 陷阱

- **host link 需 inode.cpp**:设备 ops 子类 vtable 引用 `InodeOps` 默认方法(create/mkdir/unlink/readdir 等,定义在 inode.cpp);host 集成测只 link devfs.cpp 报 undefined → 加 link inode.cpp(同 vfs_mount)。
- **signed char 坑**:`char buf` 默认 signed,`buf[8]=0xFF`→-1,`static_cast<int>`=-1≠255;测试 buffer 用 `unsigned char`。
- **mount 幂等**:重复 `mount()` 累加节点(非幂等)→ 加 `node_count_>0` 守卫(对齐 ramdisk mount 重置语义)。

## 验证

host 单测 **19/0**;run-kernel-test-all 两 leg **974/0**(单核 + -smp2 AP 回读 PASS)。零 InodeOps 接口改动。
