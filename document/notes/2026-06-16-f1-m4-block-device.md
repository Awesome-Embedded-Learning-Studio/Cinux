# 2026-06-16 F1-M4 块设备抽象 — 里程碑总结

> 里程碑封面。分批细节见 `document/ai/PLAN.md` F1-M4 章节。

## 目标

定义最小同步块设备接口 `IBlockDevice`，将 ext2 从 AHCI 硬编码中解耦，并收编 ext2 自有的 ad-hoc DMA（`g_pmm.alloc_page + g_vmm.map(EXT2_DMA_VIRT_BASE)`，M3 同类遗留）。为 NVMe/VirtIO/Page Cache 提供统一接入点（不引入请求队列/异步，留后续）。

## 产出（`kernel/drivers/`，命名空间 `cinux::drivers`）

| 类型 | 文件 | 职责 |
|------|------|------|
| `IBlockDevice` | `block_device.hpp` | 最小同步块设备抽象：`read_blocks/write_blocks/flush/block_count/block_size`，纯内核内部走 `ErrorOr<void>` |
| `RAMBlockDevice` | `ram_block_device.hpp` | 内存实现（`Heap::alloc/free` 配对，move-only），测试桩 / ramdisk 后备 |
| `AHCIBlockDevice` | `ahci/ahci_block_device.hpp/.cpp` | 薄包装 `AHCI::read/write`，持单个 `DmaBuffer`（M3 `g_dma_pool`），不碰 `ahci.cpp` 本体 |

Ext2（`kernel/fs/`）解耦：构造 `AHCI&,port` → `IBlockDevice*`，淘汰 `dma_buf_phys_/virt_/dma_ready_/ensure_dma_buffer`，`dma_buf_virt_` → `block_buf_[4096]` 普通数组。

## 关键设计

- **接口走 ErrorOr**：`IBlockDevice` 是纯内核内部接口（不跨 syscall trap），失败走 `ErrorOr<void>`（`Error::IOError`）而非 bool（A.6）。不沿用 todo 草案的 bool legacy。ext2 内部 `read_block` 保 bool（渐进迁移同 M0），仅 `dev_->read_blocks` 用 ErrorOr。
- **block_buf_ 固定数组**：Ext2 scratch buffer 用 `uint8_t block_buf_[4096]`（固定，免 heap alloc/析构），容纳任何 ext2 block（≤4096）。替代 DMA 映射的 `dma_buf_virt_`。
- **AHCIBlockDevice 复用 DmaPool**：持单个 `DmaBuffer`（一页 8-sector 容量），read 经 DMA phys → virt memcpy 到 buf，write 反向。不碰 `ahci.cpp`（其内部 ad-hoc DMA 迁 DmaPool/PrdtBuilder 留 F5-M1）。

## 关键教训（记入 PLAN GOTCHA #8）

**QEMU AHCI 容量 ≠ ext2.img 文件大小**。批2 round-trip 写 sector 7000（ext2.img 文件 8192 sector、7000 文件层面在内）仍 `[AHCI] command timeout`——QEMU AHCI IDENTIFY 报告的容量几何 < 文件大小，越界写不响应。修复：真机写测试用已知可写的低 sector（`test_ahci_write` 的 sector 2000）+ 读原值/写回 restore，避免破坏 ext2（后续 ext2 套件同次运行依赖干净盘）。

诊断副产物：QEMU 日志含控制字符使 grep 需 `-a`（强制文本）才匹配。

## 验证

run-kernel-test **694 → 705**（M4 +11：批1 RAMBlockDevice 7 + 批2 AHCIBlockDevice 4；批3 ext2 解耦重构，测试数不变）。全 QEMU in-kernel + 全量 `cmake --build build` exit 0（host 单测不破，GOTCHA #1）。

## 遗留

- **ahci.cpp 内部迁移**（F5-M1）：`execute_command` 等的手动 DMA → `g_dma_pool`/`PrdtBuilder`；`AHCIBlockDevice::block_count()` 精确值待 ATA IDENTIFY；`flush()` 真命令（当前默认空）。
- **IBlockDevice 下游**：NVMe/VirtIO Block（F5）、Page Cache（F2-M4，可能加 async/请求队列）。

---

commits（feat/f1-m4-block-device）：`0d48abf`（批1 接口 + RAM 桩）、`975582b`（批2 AHCI 适配器）、`2595eb5`（批3 Ext2 解耦）+ 批4 收尾 docs。
