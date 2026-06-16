# 2026-06-16 F1-M3 DMA 基础设施 — 里程碑总结

> 里程碑封面。分批细节见 [批1 DmaBuffer](2026-06-16-f1-m3-dma-buffer.md)、[批2 DmaPool](2026-06-16-f1-m3-dma-pool.md)、[批3 PrdtBuilder](2026-06-16-f1-m3-prdt-builder.md)。

## 目标

提供设备无关的 DMA 基础设施，收编散落的 ad-hoc DMA（`g_pmm.alloc_pages` + 硬编码 `+0xFFFFFFFF80000000ULL` 偏移 + 手动 `g_vmm.map`），为下游驱动（F5-M1 AHCI 等）统一收口。

## 产出（`kernel/drivers/dma/`，命名空间 `cinux::drivers::dma`）

| 类型 | 文件 | 职责 |
|------|------|------|
| `DmaBuffer` | `dma_buffer.hpp` | move-only 句柄：phys/virt 配对 + size，RAII 析构经 `DmaReleaseFn` 回调归还 |
| `DmaPool` | `dma_pool.hpp/.cpp` | `alloc(size)→ErrorOr<DmaBuffer>`，封装 PMM（页分配）+ VMM（map），复用 direct-map 永久映射 |
| `PrdtBuilder<MaxSegments>` | `prdt_builder.hpp` | 设备无关 scatter-gather segment 构建器，按段上限拆分 |

## 关键设计

- **RAII release 回调解耦**（批1）：`DmaBuffer` 析构归还逻辑在 DmaPool，用函数指针回调（非前向依赖 DmaPool）→ header-only 无循环依赖。
- **复用 direct-map**（批2）：`virt = phys + KERNEL_VMA`（phys 唯一决定 virt，免 virt 分配器、无泄漏）；`VMM::map` 覆盖/建立 PTE（demand-paged 下安全）。
- **设备无关 segment**（批3）：`PrdtBuilder` 输出通用 `DmaSegment`，AHCI/NVMe/VirtIO 各转硬件格式。

## 关键教训（记入 PLAN GOTCHA #7）

**direct-map（phys+KERNEL_VMA）勿 unmap**。首版 DmaPool.free 做 `vmm.unmap(virt)` → 拆掉 direct-map 永久槽 → 后续 demand paging 反复映射错 phys → **死循环 QEMU 卡死**。修复：free 只 `free_pages(phys)`，绝不 unmap（AHCI 同款：map 不 unmap）。另有 free_page_count 因页表 demand-page 开销不精确对称，测试断方向不断绝对。

## 验证

run-kernel-test **662 → 694**（M3 +20：DmaBuffer 7 + DmaPool 6 + PrdtBuilder 7）。全 QEMU in-kernel，无 host 单测涉及。

## 遗留

- **ahci.cpp 迁移**（F5-M1）：手动 PMM/VMM → `g_dma_pool`；单 PRDT → `PrdtBuilder` scatter-gather + 中断驱动。
- **`IBlockDevice`**（M4）：块设备抽象，`AHCIBlockDevice` 适配器。
- **virt 回收**：当前 free 只回收 phys（direct-map PTE 永久，无泄漏）。若未来需 <4 GiB bounce buffer 等专属映射，再加独立 virt 区。

---

commits（feat/dma）：`18ae953`（启动）、`49b7413`（批1 DmaBuffer）、`fd65b4c`（批2 DmaPool）、`6426417`（批3 PrdtBuilder）+ 各批 docs。
