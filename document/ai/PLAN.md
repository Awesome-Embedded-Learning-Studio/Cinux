# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M3 = DMA 基础设施 ✅ 完成（2026-06-16）**。
> **F1-M4 = 块设备抽象 ✅ 完成（2026-06-16）**。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

## ✅ M2（内核日志）已完成 — 2026-06-16

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | ConcurrentRingBuffer（kernel/lib/，MPSC irq_guard Spinlock）+ 测试 | ✅ | 974e406 | 667/0（+5）|
| 批2 | KernelLog（LogEntry ring + klog_*宏 + kprintf 攒行 sink）+ 测试 | ✅ | d2936a6 | 671/0（+4）|
| 批3 | sys_dmesg（SYS_dmesg=103，格式化历史读取）+ 测试 | ✅ | 4b3b95f | 674/0（+3）|
| 批4a | KernelLog::log 加实时 console 输出（reentrancy guard 避双重） | ✅ | cbcbb3a | 674/0 |
| 批4b | exception_handlers [FATAL]/[EXCEPTION] → klog_error/warn | ✅ | 809bf7d | 674/0 |
| 收尾 | 文档(本文+ROADMAP+todo+document/notes) + 全量 run-kernel-test | ✅ | (本次) | 674/0 |

dmesg 全链路闭环：`kprintf`/`klog_*` → KernelLog ring（IRQ 安全）→ `sys_dmesg` 格式化 `[LEVEL] tick: msg` 给用户态。ConcurrentRingBuffer 落地（M1 推迟的 MPSC 封装）。`klog_*` 经批4a 实时 console + ring；exception 高价值 error 迁 `klog_error/warn`（API 统一）。`kprintf` 全量迁移（294 除 mini）留后续渐进。662 → 674（+12 新测试）。

## ✅ F1-M3（DMA 基础设施）已完成 — 2026-06-16

> 目标：设备无关 DMA 基建，收编 ad-hoc（PMM + VMM + 硬编码 phys→virt 偏移 `0xFFFFFFFF80000000ULL`）。下游契约 = F5-M1 AHCI（`DmaPool.alloc()→DmaBuffer` + `PrdtBuilder`）。
> 决策：PrdtBuilder 纳入 M3（批3）；归属 `kernel/drivers/dma/`；phys→virt 用 `VMM.map`（批2 定）。
> 不碰 `ahci.cpp`(F5-M1)/`IBlockDevice`(M4)；不动早期启动 ad-hoc（PMM 就绪前用不了，OPEN GOTCHA #5 同类启动序约束）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | DmaBuffer（move-only，phys/virt/size，RAII release 回调）+ 测试 | ✅ | 49b7413 | 681/0（+7）|
| 批2 | DmaPool（`ErrorOr<DmaBuffer>`，封装 PMM+VMM，复用 direct-map 永久映射）+ 测试 | ✅ | fd65b4c | 687/0（+6）|
| 批3 | PrdtBuilder（设备无关 scatter-gather segment 构建器）+ 测试 | ✅ | 6426417 | 694/0（+7）|
| 批4 | 收尾：memory_layout.hpp 注释语义化 + M3 总结 + 全量验证 | ✅ | (本次) | 694/0 |

**完成总结**（662→694，M3 +20）：DMA 三件套落地——`DmaBuffer`（move-only 句柄，phys/virt 配对，RAII release 回调）+ `DmaPool`（`ErrorOr<DmaBuffer>`，封装 PMM+VMM，复用 direct-map 永久映射，免 virt 分配器）+ `PrdtBuilder`（设备无关 scatter-gather segment 构建器）。架构：A.6 `ErrorOr`（PMM/VMM bool→Error 转译）；A.7 不入 Cinux-Base（依赖 PMM/VMM）；命名空间 `cinux::drivers::dma`。关键教训 GOTCHA #7（direct-map 勿 unmap）。下游 F5-M1 AHCI 契约就绪（`g_dma_pool.alloc()` + segment→`HBAPrdtEntry`）；ahci.cpp 迁移留 F5-M1，`IBlockDevice` 留 M4。

## ✅ F1-M4（块设备抽象）已完成 — 2026-06-16

> 目标：最小化同步 `IBlockDevice` 接口，解耦 ext2 与 AHCI，收编 ext2 自有 ad-hoc DMA（`g_pmm.alloc_page + g_vmm.map(EXT2_DMA_VIRT_BASE)`，M3 同类遗留）。
> 决策：接口走 `ErrorOr<void>`（纯内核内部，A.6；`Error::IOError` 已就绪），不沿用 todo 草案 bool；ext2 内部 `read_block` 批3 暂保 bool（渐进迁移同 M0 FS 层），仅 `dev_->read_blocks` 用 ErrorOr，避免 ~20 处调用点同改签名撑爆批3。
> 含 `AHCIBlockDevice` 薄适配器（M4 闭环真机验证，**不碰 `ahci.cpp` 本体**——内部 DmaPool/PrdtBuilder 重构留 F5-M1）。不引入请求队列/异步 I/O、Page Cache（→F2-M4）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `IBlockDevice` 接口（`kernel/drivers/block_device.hpp`，`ErrorOr<void>`）+ `RAMBlockDevice` 测试桩 + 单测（读写 round-trip / block_count·size / 越界） | ✅ | 0d48abf | 701/0（+7） |
| 批2 | `AHCIBlockDevice` 适配器（持 `DmaBuffer`/M3 `g_dma_pool`，不碰 ahci.cpp）+ 真机单测（读 sector 0） | ✅ | 975582b | 705/0（+4） |
| 批3 | Ext2 `AHCI&,port`→`IBlockDevice*` + 淘汰 `dma_buf_phys_/virt_/dma_ready_/ensure_dma_buffer` + ~20 处 `dma_buf_virt_` 重构 + init.cpp/test 接线 + host 全量编译 | ✅ | 2595eb5 | 705/0（重构，数不变） |
| 批4 | 收尾：ROADMAP/PLAN/todo/`document/notes` + 全量 run-kernel-test | ✅ | (本次) | 705/0 |

**完成总结**（694→705，M4 +11）：块设备抽象落地——`IBlockDevice`（最小同步接口，`ErrorOr<void>`，A.6）+ `RAMBlockDevice`（内存桩，Heap 配对 move-only）+ `AHCIBlockDevice`（薄包装 AHCI，复用 M3 `g_dma_pool` 的 `DmaBuffer`，不碰 ahci.cpp）+ Ext2 解耦（`AHCI&,port`→`IBlockDevice*`，淘汰 ad-hoc DMA `ensure_dma_buffer`，`dma_buf_virt_`→`block_buf_[4096]` 普通数组，净减 29 行）。架构：A.6 ErrorOr 接口；ext2.hpp 不再依赖 ahci（更解耦）。关键教训 GOTCHA #8（QEMU AHCI 容量边界）。`block_count()` identify + `flush()` 真命令 + ahci.cpp 内部 DmaPool 迁移留 F5-M1。**F1（内核基础设施）全部里程碑完成**。

## OPEN GOTCHAS（跨里程碑通用，活警告）
1. **验证 target**：内核改动用 run-kernel-test（~694 项）；host 单测（`test/unit/`）不在其中，改被 mock 类后 push 前补全量编译（L5）。
2. **Cinux-Base 是子模块**：`Logger`/`LogLevel`/`RingBuffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`，复用勿重写。
3. **里程碑区分「类型就绪」vs「内核消费」**（M0/M1/M2 同构）：Cinux-Base 类型常先就绪，待办是 kernel/ 增量消费。
4. **klog_* 实时输出的 reentrancy guard**（批4a）：`g_klog_emit_depth` 让 klog sink 跳过 log 自身的 kprintf，避免同一行双重入队——是 kprintf→klog_* 迁移的前提（否则迁后丢 console）。定义须在 `log()` 前。
5. **崩溃 ring 读不到**：fatal_halt/kpanic 死循环，ring 历史崩溃后读不到；非崩溃异常(#DB/#BP/#GP-user)/error 进 ring 可 dmesg 读。kpanic/dump 保留 kprintf（实时诊断/halt）。
6. **kprintf 全量迁移未做**：294 个（除 mini 148）留渐进；mini 148 不迁（早期启动无 ring）。vkprintf_impl 第三参是 va_list 非 va_args，需 va_list 或手动 format。
7. **direct-map（phys+KERNEL_VMA）勿 unmap**（批2 教训，2026-06）：direct-map 是 phys↔virt 永久固定映射 + demand-paged，整个 kernel 共用。DMA/映射代码 free 时只回收 phys（`free_pages`），**绝不 `vmm.unmap(phys+KERNEL_VMA)`**——拆槽会让后续 demand paging 反复映射错 phys 死循环（QEMU 卡死，DmaPool 首版教训）。AHCI 同款（map 不 unmap）。free_page_count 因页表 demand-page 开销不精确对称，测试断方向不断绝对。
8. **QEMU AHCI 容量 ≠ ext2.img 文件大小**（M4 批2 教训，2026-06）：写高号 sector（如 7000，文件层面 8192 sector 内）仍 `[AHCI] command timeout`——QEMU AHCI IDENTIFY 报告的容量几何 < 文件大小，越界写不响应。真机写测试用已知可写的低 sector（`test_ahci_write` 的 sector 2000 = ext2 块 1000）+ 读原值/写回 restore，避免破坏 ext2（后续 ext2 套件同次运行依赖干净盘）。`AHCIBlockDevice::block_count()` 精确值待 F5-M1 ATA IDENTIFY。
