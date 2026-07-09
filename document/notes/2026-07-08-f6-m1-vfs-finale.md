# F6-M1 VFS 增强全收收官(2026-07-08)

> 分支 `feat/f6-m1-vfs-finale`,从干净 main `61a583e`,6 commit(`fb8aa48`→`3ce5e10`),两 leg run-kernel-test-all **937/937**。M1 真 ✅。

## 起因

F6-M1(VFS 增强)的 todo `00-vfs-enhance.md` 长期滞后——核对代码发现 T2 symlink / T3 硬链接在 F-ECO 批2 **已做透**(`InodeOps` `readlink`/`symlink`/`link` 虚方法 + `vfs_lookup` 完整 follow + 40 层循环检测 + `ext2_links.cpp` 真 `Ext2::symlink`/`link` + 4 syscall 全注册),但 todo 仍标"未启动"。真留续三项:T1 Dentry Cache(无 `dentry.hpp`)/ T4 flock(无 `SYS_flock`)/ T5 mount factory(`sys_mount` 只认 `tmpfs`)。

**用户决策**:M1 全收(含 Dentry Cache)+ mount factory **全做含 `/dev/sda1`**(块设备节点+注册表+source 解析)。

## 5 批(实际 6 commit)

### B0 立项 docs(`fb8aa48`)
校准 todo `00-vfs-enhance`(T2/T3 ✅ F-ECO + T1/T4/T5 实做项)+ PLAN 立项段 + ROADMAP F6-M1 细化。docs-only。

### B1a mount factory proc/devfs 单例(`3f3c108`)
`do_mount_kernel` fstype 工厂从只认 tmpfs 扩到 proc/devfs(共享 boot 静态单例,owned=false:umount 只摘 mount point 不释放单例)。`ProcFs`/`DevFs` 加 `instance()` 访问器 + `is_mounted()` 判(factory 不暴露未 init 单例)。
⭐ **GOTCHA**:run-kernel-test 的 slim boot 不跑 `procfs::init()`/`devfs::init()`(在 `CINUX_MUSL_HELLO_SMOKE` 块内),所以 `instance()` 最初返未 init 单例 → factory mount 半成品 → 机制测查 `/proc` 失败。修:`is_mounted()` 判(ProcFs `mounted_`/DevFs `node_count_>0`)+ 机制测主动 `procfs::init()`(`mount()` 幂等)。

### B1b ext2/ext4 块设备链(`1c2d337`)
`sys_mount -t ext2/ext4` 完整链:source 路径(`/dev/sda`)→ `vfs_lookup` → `Inode*` → `InodeOps::block_device()` → `IBlockDevice*` → `new Ext2` → mount → `vfs_mount_add`。
- **BlockRegistry**(name→`IBlockDevice*`,Spinlock,boot 注册 NVMe/AHCI/VirtIO)。
- **DevFS** `add_block_node`/`BlockDevOps`(块设备节点 `block_device()` override 返设备,`/dev/<name>` 投影注册表)。
- **`InodeOps::block_device()`** 虚方法(默认 nullptr,现有 FS ops vtable 多一项不动)。
- errno `kEnxio`(ENXIO=6,source 非块设备)。
- boot:`init.cpp` 注册 rootfs backing `sda` + VirtIO-blk `sdb`;`main_test` 注册 AHCI port1 ext2 盘 `sda`;`devfs_init` 遍历 BlockRegistry publish `/dev/<name>`。
⭐ ext4 复用 Ext2(extent inode flag 路由,`ext2_extent.hpp`),无独立 Ext4 类——`sys_mount -t ext4` 走同 factory。

### B2 flock POSIX 文件锁(`fa1c31b`)
`sys_flock(73)` + `FileLockManager`(全局锁表 key=`Inode*`/owner=`Task*`,SH/EX/UN/NB)。冲突阻塞(`prepare_to_wait`+`schedule_blocked`+全局 wait queue,release `wake_all`)。非阻塞 LOCK_NB 冲突返 EAGAIN。close 钩 `sys_close` 释放该 task 在该 inode 的所有锁。
机制测 5:EX 互斥 NB / SH 共享 / SH vs EX / 同 task upgrade / close 释放(栈 Inode + sentinel owner 指针;阻塞路径需第二 task 不测留生产)。
⭐ **GOTCHA**:close 钩放 `sys_close`(非 `FDTable::close`),因 `file.cpp` host-link(加 scheduler 依赖破坏 host test)。owner=Task* 简化(Linux per-open-file-description defer)。CLONE_FILES close UAF 竞态 + task-exit flock 清理留续。

### B3 Dentry Cache(`3ce5e10`)
`DentryCache`(全局 hash `(parent Inode*, name)`→`child Inode*`,正缓存 `inode_ref` pin child 防 UAF,`invalidate` 删+unref,**无 LRU 收缩留续**)。`vfs_lookup` 集成(`lookup_child` 前查命中跳磁盘 lookup,未命中调后填)。失效挂 syscall 层(`sys_unlink`/`sys_rmdir`/`sys_rename` 成功后 `invalidate`,父 `inode_unref` 前保留 parent 存活)——**FS 层不知 dentry**。
key 稳定性:各 FS inode 唯一(ext2 `inode_cache`/tmpfs `TmpNode` 内嵌/procfs/devfs 固定池),所以 `(parent Inode*, name)` 稳定。
机制测 5:add/lookup 命中+未命中+invalidate+absent-noop+不同 parent 隔离(栈 Inode sentinel)。
⭐ **头号风险**:dentry × inode_cache race(F-DYN-COV 批3 刚治 `inode_cache_` SMP race)。dentry 持 `inode_ref`、`get_cached_inode` 内部也 `inode_ref`,**两层 ref 独立**;`invalidate` 走 `inode_unref` 正常 release,不 double-free。-smp 2 验证无回归(937/937 无 panic/race/UAF)。

## 留续(非 M1 阻塞)
- **flock**:per-open-file-description owner 语义(dup 共享)/ task-exit flock 清理 / CLONE_FILES close UAF 竞态 / per-inode wait queue(现全局粗)
- **dentry**:LRU 收缩(现无上限 boot 累积)/ 负缓存(lookup NotFound 不缓存)/ 跨 mount 边界 dentry
- **F6-M5** ext4 写(extent 分配+journal)/ **F6-M6** ext2 独立库(另两弧)

## 验证
`run-kernel-test-all` 两 leg **937/937**(单核 + -smp 2),零 FAIL/panic/race/UAF。机制测 +14(test_mount 扩 proc/devfs/ext2 + test_flock 5 + test_dentry 5)。改 `InodeOps` 公共接口(B1b `block_device()` 虚方法)push 前补全量 `cmake --build` + `test_host`(CI 盲区:run-kernel-test 不编 test/unit)。

## 关键教训
- **todo 滞后要核对代码**:F6-M1 T2/T3 误判"未启动"实已做透,F-ECO 批2 落地没回填 todo。立项先 grep 代码证伪 todo。
- **slim boot vs full boot**:run-kernel-test 的 slim boot 不跑 musl-hello-smoke 块(devfs/procfs init 在那),机制测不能假设 /proc /dev 预挂;用 `is_mounted()` 判 + 主动幂等 init 兜底。
- **close 钩子位置 × host-link**:`file.cpp` host-link,加 kernel-only 依赖(scheduler)破坏 host test;close 钩放 syscall 层(`sys_close`)避开。
- **两层 inode_ref 独立**:dentry 持 ref + inode_cache 内部 ref,设计时确认两层 ref 不 double-free,`invalidate` 走正常 `inode_unref` release 路径。
