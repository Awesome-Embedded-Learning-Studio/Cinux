# F-DYN-COV 批3 — inode_cache_ race 治(inode_cache_lock_ + assert_held)

**日期**：2026-07-08
**分支**：feat/boost_cinux（批3，未 push）
**前序**：批2（bf2d2d7）RACE_TOUCH 验证靶子——用户 GUI -smp2 跑抓到 `[SMP-RACE] ext2.inode_cache: cpu1 touched after cpu0` 凶手栈 `get_cached_inode ← execve /bin/sh`。**工具有效**。

## 修法

[ext2.hpp](kernel/fs/ext2/ext2.hpp) Ext2 类加 `mutable cinux::proc::Spinlock inode_cache_lock_;`（挨 inode_cache_，+ include sync.hpp）。

[ext2_inode.cpp](kernel/fs/ext2/ext2_inode.cpp) `get_cached_inode` 入口 `auto g = inode_cache_lock_.guard();` 串行化 walk/evict/insert。**全程持锁含 read_disk_inode 盘 IO**（cache miss 稀有且慢，drop lock 需 TOCTOU recheck，不值）。删 RACE_TOUCH watchpoint（加真锁后两 CPU 串行访问，watchpoint 跨 CPU 交错会误报），改 `lockdep_assert_held(&inode_cache_lock_)` 防回归。

## 修批1 old-style cast

[race_detect.hpp](kernel/proc/race_detect.hpp) `lockdep_assert_held` 宏 `(void*)(lock)` → `static_cast<const void*>(lock)`。批1 宏定义有这 bug 但没人调用没触发；批3 ext2_inode.cpp 调用 `lockdep_assert_held(&inode_cache_lock_)` 触发 `-Wold-style-cast -Werror`。

## 验证

- build-verify RACE_DETECT=ON + LOCKDEP=ON 双 leg run-kernel-test-all 绿（单核 L7145 + -smp2 L14307），无 [SMP-RACE]、无 lockdep 误报、无 terminating（shootdown 没偶发卡）。
- test kernel ext2 test：get_cached_inode 持锁，BSP 单 CPU 无竞争，assert_held 过。
- **production -smp2 gcc smoke 待用户验**：期望不再 [SMP-RACE] kpanic、不再因 inode_cache_ race 崩（闭环阻塞 gcc 的崩）。

## 锁序

`inode_cache_lock_` → NVMe `io_lock_`（get_cached_inode 持锁调 read_block → read_disk_inode → NVMe io_submit 持 io_lock_）。audit 别处无反向（io_lock_ 不碰 inode cache）。LOCKDEP 验（若开）。

## 后续

- 未集成 race（ext2 位图/sb/bgdt、AHCI、e1000、xhci）：同一基建逐个标 RACE_TOUCH → 抓 → 修。
- inode_cache_lock_ 细化（drop lock for I/O + TOCTOU recheck）是 follow-up（当前全程持锁，cache miss 慢但正确）。
