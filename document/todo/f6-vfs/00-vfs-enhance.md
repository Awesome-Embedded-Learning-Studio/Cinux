# M1: VFS 增强 — Dentry Cache + 链接 + 文件锁

> 三项 VFS 层增强：目录项缓存加速路径解析、符号/硬链接支持、文件锁。

> **进度（2026-07-08 核对代码 + F6-M1 全收立项）**：
> - **T2 symlink / T3 硬链接：✅ F-ECO 批2 已做透**（原 todo 滞后标"未启动"，不准）。证据：`InodeOps` 有 `readlink`/`symlink`/`link` 虚方法（[inode.hpp:151/155/160](../../../kernel/fs/inode.hpp#L151)）；`vfs_lookup.cpp` 完整 symlink follow + `kMaxSymlinks=40` 循环检测 + restart-after-follow；[ext2_links.cpp](../../../kernel/fs/ext2/ext2_links.cpp) 真 `Ext2::symlink`（fast/long 都支持）+ `Ext2::link` + nlink 从 `i_links_count`；`sys_symlink`/`sys_readlink`/`sys_link`/`sys_lstat` 全注册。
> - **T5 mount：sys_mount/umount2 ✅**（`b078fef`，boot 挂 /tmp）；fstype 工厂只认 tmpfs，其余 ENODEV；**mount factory 全做（含 `/dev/sda1` 块设备节点+注册表）→ F6-M1 B1**。
> - **T1 Dentry Cache：⏳ 未启动**（无 `dentry.hpp`）→ **F6-M1 B3**。
> - **T4 flock：⏳ 未启动**（无 `SYS_flock`/`file_lock.hpp`）→ **F6-M1 B2**。
> - **T6 单测**：symlink/link 已有（F-ECO 批2 roundtrip）；dentry/flock 单测随 B2/B3 落。
>
> **M1 仍 🔄；本弧 = `feat/f6-m1-vfs-finale`（5 批：B0 docs / B1 mount factory / B2 flock / B3 Dentry Cache / B4 收官）。** 详见 PLAN「🔄 F6-M1 VFS 增强全收」段。

## 目标

提升 VFS 性能和 POSIX 兼容性。

## 任务清单

### T1: Dentry Cache  → F6-M1 B3

**文件**: `kernel/fs/dentry.hpp`, `kernel/fs/dentry.cpp`

缓存路径解析结果，避免每次 lookup 都遍历磁盘目录：

```cpp
struct Dentry {
    char        name[256];      // 文件/目录名
    Dentry*     parent;         // 父 dentry
    Inode*      inode;          // 关联 inode（dentry 持 inode_ref pin 防 UAF）
    FileSystem* fs;             // 所属文件系统
    bool        valid;          // 是否有效（inode 可能被删除）

    // 子 dentry 哈希表
    Dentry*     children[DENTRY_HASH_BUCKETS];
    Dentry*     hash_next;      // 同桶链

    // LRU 链
    Dentry*     lru_prev;
    Dentry*     lru_next;
};

class DentryCache {
public:
    // 查找（命中且 valid → inode_ref(inode) 返；未命中 nullptr）
    Dentry* lookup(Dentry* parent, const char* name);
    // 新建 dentry + inode_ref(inode) pin，插哈希+LRU
    Dentry* add(Dentry* parent, const char* name, Inode* inode);
    // 标 valid=false + inode_unref（unlink/rename/rmdir 时）
    void invalidate(Dentry* dentry);
    // LRU 淘汰（refcount==0 的上限踢，抄 ext2 inode_cache 软上限）
    void shrink(size_t target_count);
    size_t count() const;
};
```

- [ ] Dentry 结构体
- [ ] DentryCache 哈希查找（全局单例，Spinlock 保护，抄 `g_mount_lock` 范式）
- [ ] lookup() — 命中 inode_ref 返回，未命中返回 nullptr
- [ ] add() — 新建 dentry，`inode_ref` pin 防 UAF（复用 [file.cpp:21](../../../kernel/fs/file.cpp#L21) `inode_ref`）
- [ ] invalidate() — valid=false + `inode_unref`
- [ ] LRU 淘汰（refcount==0 上限踢，简单双向链表）
- [ ] 集成到 `vfs_lookup`（[vfs_lookup.cpp:129](../../../kernel/fs/vfs_lookup.cpp#L129) `lookup_child` 前查/后填）
- [ ] 失效挂 syscall 层（sys_unlink/rmdir/rename/umount2；**不让 FS 知 dentry**）
- [ ] ⭐ 不做负缓存（lookup NotFound 不缓存，defer）—— 避免 invalidation 复杂性

### T2: 符号链接支持  ✅ F-ECO 批2

**文件**: `kernel/fs/inode.hpp`（扩展）

- [x] InodeType 增加 `Symlink = 3`
- [x] InodeOps 增加 `readlink()` 虚方法（[inode.hpp:151](../../../kernel/fs/inode.hpp#L151)）
- [x] 路径解析遇到 symlink → 读取目标 → 重新解析（[vfs_lookup.cpp](../../../kernel/fs/vfs_lookup.cpp) 完整 follow）
- [x] 循环检测（`kMaxSymlinks = 40`，对齐 Linux MAXSYMLINKS）
- [x] symlink() syscall — 创建符号链接（`sys_symlink.cpp`）
- [x] readlink() syscall — 读取链接目标（`sys_readlink.cpp`，区分 fast-inline vs 数据块 long）
- [x] lstat() syscall — 不跟随链接的 stat（[sys_stat.cpp:108](../../../kernel/syscall/sys_stat.cpp#L108)）

### T3: 硬链接支持  ✅ F-ECO 批2

**文件**: `kernel/fs/inode.hpp`（扩展）

- [x] InodeOps 增加 `link()` 虚方法（[inode.hpp:160](../../../kernel/fs/inode.hpp#L160)）
- [x] Inode 的 nlink 字段正确维护（ext2 从 `disk.i_links_count` 读，[ext2_metadata.cpp:164](../../../kernel/fs/ext2/ext2_metadata.cpp#L164)）
- [x] link() syscall — 创建硬链接（`sys_link.cpp` → `Ext2::link`，[ext2_links.cpp](../../../kernel/fs/ext2/ext2_links.cpp)）
- [x] unlink() 减少 nlink，nlink=0 时删除文件（`ext2_dirops.cpp`）

### T4: 文件锁（flock）  → F6-M1 B2

**文件**: `kernel/fs/file_lock.hpp`, `kernel/fs/file_lock.cpp`

```cpp
enum class LockType {
    Shared,     // LOCK_SH — 共享锁（读）
    Exclusive,  // LOCK_EX — 独占锁（写）
    Unlock,     // LOCK_UN — 解锁
};

class FileLockManager {
public:
    // flock 操作（key=Inode* 冲突检测 / owner=Task* 简化，Linux per-file-description defer）
    int flock(File* file, LockType type, bool nonblock);
    // close 释放该 task 在该 inode 的所有锁（FDTable::close 钩）
    void release_task_inode(Inode* inode, Task* owner);
private:
    struct LockEntry {
        Inode*   inode;
        Task*    owner;
        LockType type;
        Task*    wait_head;   // 阻塞等待队列（net/wait_queue.hpp 范式）
        LockEntry* next;
    };
    LockEntry* locks_;  // 全局锁链表
};
```

- [ ] FileLockManager 实现（global，Spinlock + wait_queue）
- [ ] 共享锁：多个读者可同时持有
- [ ] 独占锁：只有一个持有者
- [ ] 阻塞等待（冲突 `prepare_to_wait`+`schedule_blocked`；非阻塞 LOCK_NB 返 `-EWOULDBLOCK`）
- [ ] flock() syscall（Linux syscall 73，照 sys_symlink 注册）
- [ ] 文件关闭时自动释放锁（`FDTable::close` 钩 `release_task_inode`）

### T5: mount / umount syscall  → F6-M1 B1（factory 全做）

```cpp
int64_t sys_mount(const char* source, const char* target,
                  const char* fstype, uint64_t flags, const void* data);
int64_t sys_umount(const char* target);
int64_t sys_umount2(const char* target, int flags);
```

- [x] sys_mount — 挂载文件系统到目标路径（`sys_mount.cpp`，tmpfs 堆实例化 owned=true）
- [x] sys_umount2 — ownership-aware 卸载（`b078fef`，`owned` 字段）
- [ ] **factory 扩 proc/devfs**（静态单例 `&ProcFs::instance()`/`DevFs::instance()`，owned=false）
- [ ] **factory 扩 ext2/ext4**（source→`vfs_lookup`→`Inode*`→`ops->block_device()`→`new Ext2(dev)`，owned=true；ext4 复用 Ext2 extent 路由）
- [ ] **块设备注册表**（`BlockRegistry` name→`IBlockDevice*`，boot 注册 NVMe/AHCI/VirtIO）
- [ ] **DevFS 块设备节点**（`add_block_node` + `InodeOps::block_device()` 虚方法默认 nullptr，BlockDevOps override 返设备）
- [ ] **ProcFs/DevFs `instance()` 访问器**（导出匿名 ns 的 `g_procfs`/`g_devfs`）
- [ ] 支持的 fstype：ext2, ext4, proc, devfs, tmpfs（ramfs 不存在，返 ENODEV）
- [x] mount flags：MS_RDONLY, MS_NOSUID（接受忽略，Linux ABI parity）
- [ ] libc mount/umount wrapper（follow-up）

### T6: 单元测试

- [ ] DentryCache 命中/未命中/失效（F6-M1 B3）
- [x] 符号链接解析（多层跟随）— F-ECO 批2 roundtrip
- [x] 硬链接 nlink 正确 — F-ECO 批2
- [ ] flock 共享/独占互斥（F6-M1 B2）
- [ ] 路径解析使用 dentry cache 加速（F6-M1 B3，可选 perf）

## 产出物

- [ ] `kernel/fs/dentry.hpp` / `.cpp` — Dentry Cache（F6-M1 B3）
- [ ] `kernel/fs/file_lock.hpp` / `.cpp` — 文件锁（F6-M1 B2）
- [x] symlink/readlink/link/lstat/flock syscall（前 4 个 ✅ F-ECO；flock 待 B2）
- [x] InodeOps 扩展 readlink/symlink/link（✅ F-ECO）；+ `block_device()` 虚方法（B1）
- [ ] 路径解析集成 dentry cache（F6-M1 B3）
- [ ] 块设备注册表 + DevFS 块节点 + mount factory 扩 ext2/ext4/proc/devfs（F6-M1 B1）
