# ext2 盘元数据 race 治(block_alloc_lock_ + KmBuf) + wild block trace

**日期**:2026-07-08
**分支**:feat/boost_cinux(未 push)
**前序**:批3 inode_cache_lock_ + offset_lock_ fix 后,用户 GUI -smp2 gcc smoke 还崩 wild ext2 block。

## wild block trace(memory 5 轨迹法)

加仪器 resolve_disk_block_ 返 wild(blk>=s_blocks_count)时 kprintf blk/file_block/i_block[12,13,14]/disk 地址 + backtrace。

抓到:
```
[EXT2-TRACE] resolve wild blk=2232352768 file_block=224 i_block[12]=4751 [13]=5008 [14]=0 disk=0xFFFF80000000CD30
Backtrace: resolve_disk_block_ <- Ext2FileOps::read <- PageCache::get_page <- handle_pf <- isr_pf_stub
```

cc1 demand page 读 indirect block 4751,indirect[212]=garbage(wild)。file_block=224 在 indirect 区(block_size=1024,ptrs=256,indirect 覆盖 12-267)。i_block[12]=4751 合法(cc1 的 indirect block 号),但 4751 内容 garbage。

wild 每次值不同(2232352768 → 4293913577)= 动态 garbage。

## 根因排查

加 write_block kprintf 仪器 grep 4751:
- write_block(4751) 出现 0 次 → 4751 没被 write 覆盖
- 加仪器后 race 时序错开不复现(heisenbug)→ smoke exit 0

4751 没被覆盖 → wild 疑 read_block(4751) 读错(NVMe DMA race)或 mm PageCache 残留。但 NvmeBlockDevice::lock_(adapter 层串行 dma_buf_+memcpy) + NvmeController::io_lock_(controller 层串行 SQ/CQ)两层锁该保护住。真因没完全定位(heisenbug 掩盖)。

## 修代码层已知 race

虽 wild 真因没定位,但 trace 暴露 ext2 盘元数据多处无锁 race,修代码层正确性:

1. **block_alloc_lock_**:alloc_block/free_block 位图 read-modify-write 全程无锁 → 两 CPU 同时 alloc 都读同 bit free → 都 mark+write → 同块分两文件 → 一覆盖另一。加 Spinlock 串行 alloc/free_block + alloc/free_inode。
2. **write_superblock/write_bgdt 改 KmBuf**:原用 block_buf_(共享,NOT SMP-safe),多调用方(alloc_block/free_block/alloc_inode/free_inode/ext2_directory)不持锁 race。改 per-call KmBuf。
3. **alloc_inode/free_inode 持 block_alloc_lock_**:共享 block_buf_(write_superblock/bgdt)串行 + 位图 RMW 串行。

## 留 wild 监控

resolve_disk_block_ 的 ext2_trace_wild_blk 保留(低开销:只 blk>=s_blocks_count 时 kprintf+backtrace)。作 wild block audit 常驻(类似 refcount audit),下次 wild 抓瞬间。

## 验证

- 单核 leg ALL PASSED(ext2/alloc/read/write/pipe test 过,含 test_vfs_syscall offset test + test_sys_pipe sigpipe)。
- -smp2 leg shootdown IPI 偶发卡(memory -smp 已知,非本 fix)。pipe test PASS。
- production -smp2 gcc smoke:加 write_block 仪器后 exit 0(heisenbug 时序错开)。去仪器后 race 可能偶发(wild 真因没定位)。

## GOTCHA

- heisenbug:加 kprintf 改 timing → race 时序错开不复现。memory debugging-audit-dynamic-coverage-gap:静态债审「代码对」,动态 race timing 敏感。wild 真因没定位,靠代码层 fix(位图+block_buf_)降已知 race 面。
- wild 每次值不同 = 动态 garbage(read 错或覆盖,不是静态 corrupt)。
- 4751 没被 write 覆盖(write_block(4751)=0)→ wild 疑 read_block 读错,但两层锁该保护。真因 5 轨迹法未闭合(heisenbug 掩盖)。
