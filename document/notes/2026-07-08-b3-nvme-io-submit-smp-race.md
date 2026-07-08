# B3 — NVMe io_submit SMP race 修复

**日期**：2026-07-08
**分支**：feat/boost_cinux（commit `35a981c`，未 push）
**前序**：ext2 block_buf_ race 修（`9eabf0d`）后 smoke，wild 块号没但合法块仍 NVMe failed。

## Context

ext2 race 修复后 smoke -smp2 gcc：
- block 1294（合法 < blocks 196608）NVMe failed status=0x4080。
- wild 块号（17448624）没了（ext2 race 修 ✓）。
- 但合法块 NVMe failed → demand page 失败 → segfault。

## 根因

NVMe driver `io_submit`（nvme.cpp:396）+ admin submit 无锁，`io_sq_tail_`/`io_cq_head_`/`io_cq_phase_` 共享。SMP 两 CPU 同时 io_submit → sq slot 覆盖 + completion 乱 → status=0x4080（合法 LBA 也 failed，读到别人的 completion 或 sq slot 错）。memory f5-m3 749e7db 修了别的 NVMe race，io_submit/completion poll 这层没锁。

status=0x4080 解码：`status_field>>1`，SC=0x80 SCT=0（Generic，非标准/vendor）—— wild 块号（超范围）和合法块都返它，说明不是"超范围"，是 completion/sq race 的垃圾 status。

## 修法

NvmeController 加 `io_lock_` Spinlock，io_submit 持锁（SQ enqueue + CQ poll 全在锁内）。admin submit（init 期单线程）不锁。持锁期间 poll（busy-wait，IF=0-safe；caller #PF/syscall）。

## 验证

build-verify KVM=ON 双 leg run-kernel-test-all 全绿（mechanism PASS，不回归）。production -smp2 gcc 不崩留 smoke 验证。

## SMP race 三线收尾（同模式：共享状态无锁）

- **mm buddy free**（`c255953`）：buddy free list 无锁 → free-side Spinlock。
- **ext2 block_buf_**（`9eabf0d`）：单实例 buffer 无锁 → KmBuf dst/src。
- **NVMe io_submit**（`35a981c`）：io_sq_tail_/cq_head_ 无锁 → io_lock_ Spinlock。

## 基建讨论（用户点出"全是 SMP race"）

SMP race 系统性（三线同模式）。基建选项：
- **KCSAN-style data race detector**（instrument 内存访问，runtime 抓无锁共享）—— 最直接，实现重（编译期插桩 + runtime 检测），CinuxOS 无。属 F-DYN-COV 轴（memory `debugging-audit-dynamic-coverage-gap`）。
- **manual audit driver/mm 共享状态**（AHCI/VirtIO read_blocks、PageCache、inode cache）—— 轻量，逐个修。
- **LOCKDEP**（已有 `CINUX_LOCKDEP`，sync.hpp L81）—— 锁序/死锁，**不抓 data race**（无锁共享），对这类无效。

建议：短期 manual audit driver/mm 共享状态（快），中期 KCSAN 作 F-DYN-COV 轴。
