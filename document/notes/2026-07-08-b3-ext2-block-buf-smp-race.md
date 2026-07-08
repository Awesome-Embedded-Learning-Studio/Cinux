# B3 — ext2 block_buf_ SMP race 修复（10h 偶发 gcc 崩 wild 块号线）

**日期**：2026-07-08
**分支**：feat/boost_cinux（commit `9eabf0d`，未 push）
**前序**：IPI shootdown stage1+2（defect C 修）。smoke 跑出 10h 偶发 gcc 崩，查根因。

## Context

smoke -smp 2 gcc 编译崩（/tmp/segfault.log）：
- cc1 rip=0（instruction fetch 未映射）+ NVMe I/O failed status=0x4080 slba=34897248 + EXT2 read_block(17448624) I/O failed。
- /bin/sh 写 ld-linux readOnly VMA flags=0x1（write protection，pre-existing page_fault.cpp L414 注释）。

**wild ext2 块号**：17448624 >> ext2 blocks 196608。NVMe slba=34897248 = 17448624×2（ext2 1024B block / NVMe 512B LBA）。即 memory 记录的"10h 偶发 gcc 编译崩（wild ext2 块号）"。

## 根因

ext2 `block_buf_[4096]`（ext2.hpp）是 Ext2 单实例共享 buffer，`read_block`/`write_block`（ext2_init.cpp）无锁。SMP 两 CPU 同时 demand page read（或 bitmap/inode read-modify-write）→ block_buf_ 互相覆盖 → `resolve_disk_block_`（ext2_common.cpp）`read_block(indirect_block)` 填 block_buf_ → `indirect = block_buf()` 读，中间若另一 CPU read_block 覆盖 block_buf_ → 拿到别人的数据 → wild indirect 指针 → wild block 号 → NVMe 读超范围 failed → demand page 失败 → segfault。

代码证据链确凿（block_buf_ 共享 + 无锁 + resolve race window + wild block 现象吻合）。**无 [AUDIT] panic** → 非 free race（defect A/C 不是真因），是 ext2 层 SMP race。

## 修法

`read_block(block,dst)`/`write_block(block,src)`/`zero_and_write_block(blk,src)` SMP-safe 重载（调用方提供 buffer）。原 block_buf_ 版保留 init 期（单线程）。SMP 调用方用 **KmBuf**（RAII 堆 kmalloc，循环外一次复用，析构 kfree）。

**不用栈 buf**：用户点出 IST2 只 4KB（IRQ_STACK_PAGES=1），demand-page #PF 走 IST2，栈 4KB buf 必炸；task 栈 8KB 深递归也紧。堆 kmalloc 安全。

改动：
- **ext2_common.hpp**：KmBuf RAII（放此避 ext2.hpp 超 500 行）。
- **ext2.hpp**：read_block/write_block/zero_and_write_block dst/src 重载。
- **ext2_init.cpp**：重载实现（read_block(block) 委托 block_buf_）；init superblock/bgdt 保留 block_buf_（单线程）。
- **ext2_common.cpp**：Ext2FileOps::read（demand page）+write+resolve_disk_block_ 用 KmBuf。
- **ext2_block.cpp**：bitmap alloc/free read-modify-write 用 KmBuf。
- **ext2_directory/dirops/inode/metadata**：KmBuf（unlink 用 unlink_ptr_buf_ 直读，省 memcpy）。
- **ext2_inode.cpp get_or_alloc_block**：独立 zbuf（zero 不覆盖父 array，去除原重读）。

## 验证

build-verify KVM=ON 双 leg run-kernel-test-all 全绿（单核 + -smp2 mechanism PASS；ext2 tests 不回归）。production -smp2 gcc 不崩留 smoke 验证（用户启 GUI -smp2 跑 gcc）。

## GOTCHA

1. **IST2 4KB**（IRQ_STACK_PAGES=1）：demand-page #PF 走 IST2，栈 buf 炸。用堆 KmBuf。用户点出（"万一递归深一点就给你炸干净了"）。
2. **KmBuf 放 ext2_common.hpp**（非 ext2.hpp）：ext2.hpp 既有大，加 KmBuf 超 500 行（pre-commit 拦）。移 ext2_common.hpp（126→146 行）。
3. **ext2_inode.cpp get_or_alloc_block 重构**：独立 zbuf（zero 不覆盖父 array）→ 去除原重读（block_buf_ 共享时 zero 覆盖父，需重读）。逻辑等价，注释更新。
4. **unlink 用 unlink_ptr_buf_ 直读**：`read_block(b, unlink_ptr_buf_)` 省 memcpy + 省 KmBuf。
5. **ld-linux 写 readOnly VMA flags=0x1**（L348）是独立 pre-existing（page_fault.cpp L414 注释 "ld-on-cc1-.o saga"），非 ext2 race。ext2 race 修了 wild 块号，ld-linux 写保是另一条（可能 VMA flags 错或 ld-linux relocation，留 follow-up）。

## 10h 偶发 gcc 崩收尾

- **wild ext2 块号**（本批 `9eabf0d`）：ext2 block_buf_ SMP race。修。
- **defect C**（`a330e3c`+`3a1fc45`）：CoW free race。修（IPI shootdown deferred）。
- **ld-linux 写 readOnly VMA**：独立 pre-existing，留 follow-up。
