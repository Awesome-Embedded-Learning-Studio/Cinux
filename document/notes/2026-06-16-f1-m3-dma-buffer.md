# 2026-06-16 F1-M3 DMA — DmaBuffer（批1）

## 背景

CinuxOS 现有 DMA 是 ad-hoc：[ahci.cpp](../../kernel/drivers/ahci/ahci.cpp) 里手动 `g_pmm.alloc_pages()` + 硬编码 `+ 0xFFFFFFFF80000000ULL` phys→virt 偏移 + 手动 `g_vmm.map()`，phys/virt 配对散落、重复 magic。F5-M1 AHCI DMA todo 明确要 `DmaPool.alloc()→DmaBuffer` + `PrdtBuilder`（scatter-gather），这些基建**都不存在**。M3 的任务是提供设备无关的 DMA 基础设施，让下游驱动统一收口。

批1 = 类型就绪：DmaBuffer 这个 move-only 句柄（同 M0/M1/M2 的"类型先行"模式——Cinux-Base/kernel 类型先落地，后续批增量消费）。

## 目标

DmaBuffer 把"一块 DMA-capable 内存"封装成：设备用 `phys()`、CPU 用 `virt()`，move-only 防别名，RAII 归还防泄漏。批1 纯值类型自洽（不碰 PMM/VMM），批2 DmaPool 注入 release 回调即得自动归还。

## 设计

### move-only 值类型

`phys_ / virt_ / size_` 三字段 + `release_`（回调）。禁拷贝（别名 → double release）、允许 move（move 后源置 invalid）。accessor `phys()/virt()/size()/valid()`。`detach(phys,virt,size)` 显式交出所有权（塞设备寄存器 / 传 DmaPool::free 时用）。

### RAII via release 回调（关键，避免循环依赖）

DmaBuffer 析构要归还物理页，但归还逻辑在 DmaPool（批2）。若 DmaBuffer 持 `DmaPool*` 并在析构调 `owner_->release()`，则 dma_buffer.hpp 需 DmaPool 完整定义 → 循环依赖、破坏 header-only。

解法：`using DmaReleaseFn = void(*)(const DmaBuffer&)`，纯函数指针。DmaPool（批2）提供一个 static 函数作回调注入。批1 release_=nullptr（手动管理），析构 no-op，**完全自洽可测**；批2 注入后析构自动归还。类型保持 header-only，无前向依赖。

### 命名空间 cinux::drivers::dma

同构 ahci（`cinux::drivers::ahci`）。DMA Pool 的消费者是设备驱动，归 drivers/。

## 关键决策

- **值类型不持资源所有权，pool 才持**：DmaBuffer 是 handle，物理页归 DmaPool（批2 free 回收）。这让批1 无需 PMM/VMM 即可测试。
- **release 回调 > 前向声明 DmaPool**：换 header-only + 无循环依赖，代价是一个函数指针字段（可接受）。
- **PLAN 描述微调**：原写"RAII 析构归还"，实际是"RAII release 回调"（析构调回调，回调由 pool 提供而非析构直接归还 pool）——更准，已同步 PLAN。

## 陷阱

批1 简单，无编译/运行陷阱。设计层面的注意点：move 赋值须先 `release_owned()` 释放旧值再搬新值（否则旧映射泄漏），且自赋值防护（`this != &other`）。

## 验证

run-kernel-test **674 → 681**（+7）：
- 构造/accessor、默认 invalid、move 构造转移、move 赋值、detach、RAII 析构触发回调、moved-from 源不重复 release。

纯值类型测试，无 PMM/VMM，无 host 单测涉及。

## 遗留

- 批2 DmaPool（`ErrorOr<DmaBuffer>`，封装 PMM+VMM+对齐，注入 release 回调）
- 批3 PrdtBuilder（scatter-gather）
- 批4 收尾（memory_layout 注释语义化 + 全量验证）

---

commit：`49b7413`（批1 DmaBuffer）。
