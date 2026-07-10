# F6-M6 ext2 block_buf_ SMP race 修复（2026-07-10）

> 分支 `feat/f6-m6-blockbuf-race`，从已合 M6 的 main `4a0b285` 开；commit `本次`。修复 M6 host TSAN 抓到、F-DYN-COV QEMU forensics 漏掉的共享 scratch race，并把原先因预期报 race 而移除的 concurrent lookup 测试加回永久回归。

## 背景

F6-M6 把 ext2 搬到 `libs/ext2/`，补齐 host PAL、真逻辑 ASAN 测试与 TSAN 并发测试。B2 的 parallel lookup 立即报告真实数据竞争：多个线程在同一 `Ext2` 实例上解析 `/etc/motd` 时，同时读写 `Ext2::block_buf_`。

F-DYN-COV 批3/批4修过 `inode_cache_` 与 `block_alloc_` 的数据结构 race，但没有覆盖共享 scratch clobber。lockdep 只能检查锁持有与锁序；scratch 内容互踩要等到时序相关的坏块号或目录项损坏才显现，host TSAN 则直接观察内存访问，秒级定位。

## 根因与审计

`Ext2` 保留一块 4096-byte `block_buf_` 供无 dst 的 `read_block()`/`write_block()` 使用，接口已明确标注 NOT SMP-safe。全量审计 `libs/ext2/` 的 `read_block(` 与 `block_buf_` 后确认：

- `lookup_in_dir` 在每个目录块上调用无 dst `read_block(blk)`，随后遍历共享 `block_buf_`。并发 lookup 会同时覆盖和读取同一数组，是 race 主源。
- `symlink` 直接在 `block_buf_` 写 target，再调用无 src `write_block(data_blk)`。并发 symlink 同样共享 scratch。
- `mount()` 读取 superblock/BGDT 时仍使用共享 buffer，但 mount 是发布 filesystem 前的单线程阶段；保留原 API并补充 mount-only、single-threaded 注释。
- 其余运行期 ext2 路径已经使用 per-call `KmBuf` 或调用方自有 buffer；内核测试中的无 dst API调用是串行夹具，保留不动。

## 修法

### lookup_in_dir

在目录块循环外分配一次 `KmBuf scratch(4096)`，OOM 时返回 lookup 失败。循环内复用同一调用私有 buffer，并改走 `read_block(blk, scratch.get())`；目录项遍历只读 `scratch.data()`。这样每次 lookup 只做一次 kmalloc，同时不同 CPU 不共享 scratch。

### symlink

分配调用私有 `KmBuf`，OOM 时回滚已分配的 data block 与 inode。target 写入 `scratch.data()`，落盘走 `write_block(data_blk, scratch.get())`。这同时关闭并发 symlink 的相同 race 模式。

`block_buf_` 及无 dst I/O重载继续保留，供 mount 和串行测试夹具使用，没有扩大公共接口变更。

## TSAN 回归价值

恢复 `test_ext2_concurrent.cpp` 的 parallel lookup：4 个线程共享同一个 `Ext2` + `RAMBlockDevice`，每线程执行 200 次 `ext2.lookup("/etc/motd")`。每个成功结果都调用 `inode_unref`，与 `get_cached_inode` 返回的引用配对；原测试还继续并发压 alloc/free。

这条测试从“能稳定重现已知 race 的临时诊断”变成“永久防止共享 scratch 回归的门禁”，直接兑现 M6 host-build 的价值。

## 验证

- host TSAN：`test_ext2_concurrent` **2 passed, 0 failed**；parallel alloc/free 与 4×200 parallel lookup 均通过，**0 ThreadSanitizer warning**，退出 0。
- kernel SMP：`timeout 200 cmake --build build --target run-kernel-test-all -j$(nproc)`；单核与 `-smp 2` 两 leg 各 **937 passed, 0 failed**，SMP AP wake/shootdown/race-detect PASS，退出 0。
- host 全套：`cmake --build build-host --target test_host -j$(nproc)`，**66/66**。
- 全量构建：`cmake --build build -j$(nproc)`，成功。
- host ASAN：`test_ext2_host` 真逻辑 **1 passed, 0 failed**，退出 0。

## 结论

并发 lookup 与 symlink 不再触碰共享 `block_buf_`；mount 期的单线程用途被显式标注。M6 的首要留续项闭环，剩余非阻塞项为 ext2 STATIC target 的 codegen 分离与 F6-M5 ext4 写路径。
