# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M3 = DMA 基础设施 ✅ 完成（2026-06-16）**。
> **F1-M4 = 块设备抽象 ✅ 完成（2026-06-16）**。
> **F5-M1 = AHCI DMA 迁移 ✅ 完成（2026-06-16）**。
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

## ✅ F5-M1（AHCI DMA 迁移）已完成 — 2026-06-16

> 目标：收编 ahci.cpp 内部 ad-hoc DMA（command list/FIS/command table 的手动 `g_pmm`+`g_vmm.map`+硬编码 `+0xFFFFFFFF80000000ULL`，[ahci.cpp:132-191](kernel/drivers/ahci/ahci.cpp#L132-L191)）+ execute_command 手动 PRDT（[ahci.cpp:256-265](kernel/drivers/ahci/ahci.cpp#L256-L265)）→ M3 `DmaPool`/`PrdtBuilder`，闭环 block device→AHCI DMA 栈；补 `AHCIBlockDevice` 的 `block_count()`（ATA IDENTIFY）+ `flush()`（FLUSH CACHE）M4 占位。
> 决策：批1 保持 setup_port DMA 布局不变（command list+tables 同 4KB DmaBuffer、FIS 单独），只换来源 → DmaPool（低风险）；identify 容量先 28-bit（words 60-61）；AHCI::read/write 保 bool（legacy 渐进），identify/flush 新方法走 ErrorOr。
> 不做：AHCI 中断驱动（仍轮询）、NCQ、VirtIO/NVMe（F5-M2/M3）。BAR5 MMIO 映射（[ahci.cpp:65](kernel/drivers/ahci/ahci.cpp#L65)）是设备寄存器非 DMA，保留。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | setup_port DMA 收编：command list+tables / FIS 手动 PMM+VMM+硬编码偏移 → `g_dma_pool`（DmaBuffer per-port 成员，替 `cmd_list_phys_`/`fis_buf_phys_`），布局不变。GOTCHA #7（release 只 free phys） | ✅ | b97bb1d | 705/0（重构，数不变） |
| 批2 | execute_command PRDT 手动 `prdt[0]` → `PrdtBuilder`（scatter-gather，单段也用，输出→`HBAPrdtEntry`） | ✅ | f583f5e | 705/0（重构，数不变） |
| 批3 | ATA IDENTIFY（`AHCI::identify`→容量，`block_count()` 真值）+ FLUSH CACHE（`AHCI::flush`，`flush()` 真命令） | ✅ | fdaea2a | 705/0（identify/flush 真机过） |
| 批4 | 收尾：ROADMAP/PLAN/todo/notes + 全量验证（FORTIFY 对等） | ✅ | (本次) | 705/0 |

**完成总结**（705→705，F5-M1 重构+新功能，测试数不变）：ahci.cpp 内部 ad-hoc DMA 全收编 M3 基建——`setup_port`（command list+tables/FIS → `g_dma_pool` DmaBuffer per-port，替手动 PMM+VMM+硬编码 `+0xFFFFFFFF80000000ULL`，删未用 MMIO 映射）+ `execute_command` PRDT（手动 `prdt[0]` → `PrdtBuilder` scatter-gather）+ ATA IDENTIFY/FLUSH CACHE（`execute_command`/`build_cfis` 参数化 `command` byte；`AHCI::identify` 解析 words 60-61 28-bit 容量 → `AHCIBlockDevice::block_count()` 真值；`AHCI::flush` → `AHCIBlockDevice::flush()` 真命令）。**闭环 block device→AHCI DMA 栈**：M3 DmaPool/PrdtBuilder 真正落地到 AHCI 驱动。验证 QEMU 真机 `block_count()>0`（IDENTIFY 过）+ flush 不崩 + FORTIFY 对等（本地复现 CI）。遗留：中断驱动（仍轮询，todo 目标 3）留后续。

## 🔄 F2-M1（VMA 区域记账）— 进行中（2026-06-16 起）

> 目标：给 `AddressSpace` 补 VMA（Virtual Memory Area）区域记账——追踪每进程"哪段虚拟地址 / 什么权限 / 匿名还是文件映射"，为 mmap/munmap/brk/demand paging/CoW 提供单一事实源。M1 只做记账 + 让 PF handler 用它做合法性校验（无 VMA 命中 → 真 segfault），不做 mmap（→M2）/brk（→M3）。
> 决策（propose 已确认）：
> - **#1 范围含闭环**（批1-4，PF 校验是 VMA 真价值；否则账本成死数据到 M2 才用）。
> - **#2 `IVMAStore` 走 `ErrorOr`**（A.6，新代码无 legacy，非 todo 草案 bool）；`AddressSpace::map` 仍 bool（渐进迁移同 M4 批3）。
> - **#3 VMA 结构体预留 `backing_inode`/`file_offset`**（forward-decl `fs::Inode`），M1 只测匿名区域，文件映射填值留 M2。
> - **#4 批1 单测走 `kernel/test/`**（计数进 run-kernel-test，同 M3 惯例）。
> **实机冒烟（用户要求）**：批4 收尾 run-kernel-test 全绿后，`timeout` 拉起真内核（`make run`，GUI 启动 + 用户程序）确认不炸——防"kernel-test / host-test 三绿但一进真内核就炸"（PF/execve 改动高危）。非完成门（GUI 无断言），观察性保险。
> 依赖就绪：kernel heap（new VMA）/ Spinlock irq_guard（M1 RingBuffer 同款）/ ErrorOr（M0）。
> 不做：mmap/munmap/mprotect（M2）、brk（M3）、Page Cache（M4）、demand paging 增强（M5）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `vma.hpp/cpp`：`VmaFlags` + `VMA` 结构体 + `IVMAStore` 抽象 + `LinkedListVMAStore`（insert 有序+合并 / find / remove 拆分 / find_free_area）+ 单测 | ✅ | — | 710/0（+5） |
| 批2 | `AddressSpace` 持 `LinkedListVMAStore`+`Spinlock` 成员（`vmas()`/`vma_lock()` 访问器，构造建/析构 RAII）+ `LinkedListVMAStore` 补 move（AddressSpace move-only 需成员可 move）+ `memory_layout` 加 `USER_BRK`/`MAP` 常量（按实际栈顶≈32GB 校正，非 todo 127TB） | ✅ | — | 712/0（+2） |
| 批3 | execve ELF 段（PF_W/PF_X→VmaFlags）+ init/gui_init 用户栈（Stack flag）注册 VMA；insert 失败→路径失败保 VMA 完整 | ✅ | — | 712/0（启动路径未被 run-kernel-test 执行，靠批4 实机） |
| 批4 | PF demand paging 加 VMA `find()` 诊断（未命中 klog_warn 但仍 demand page，不改行为；真 segfault 留 M5）+ 收尾 + 实机冒烟 | ✅ | — | 712/0 + 实机启动不炸 |

**完成总结**（705→712，F2-M1 +7）：VMA 记账基础设施落地——`LinkedListVMAStore`（侵入式有序链表，insert 合并 / remove 拆分 / find / find_free_area，store-owns RAII）+ `IVMAStore` 抽象（可换红黑树）+ AddressSpace 集成（值成员 `vma_store_` + `Spinlock`，补 move）+ execve/栈注册（PF_W/PF_X→VmaFlags；Stack flag）+ PF demand paging VMA `find()` 诊断（未命中 warn 不改行为，真 segfault 留 M5）。架构：A.6 ErrorOr（逻辑错误）；A.7 不入 Cinux-Base（依赖 heap）；用户布局常量按实际栈顶≈32GB 校正（非 todo 草案 127TB）。关键教训：operator new 返 nullptr 非 panic（OOM 崩惯例）；klog_warn 是宏禁加命名空间前缀；启动路径不被 run-kernel-test 覆盖（靠实机冒烟）。遗留：PF 硬门控（M5）/ fork VMA 复制（F3）。

## 🔄 F2-M2（mmap/munmap/mprotect）— 进行中（2026-06-17 起）

> 目标：实现 mmap/munmap/mprotect syscall（Linux 9/11/10），消费 M1 VMA（`find_free_area`+`insert` / `remove` / flags），让用户程序动态内存映射。mmap 懒分配（仅建 VMA，PF 时 demand page，兼容 M1 批4 诊断）。
> 决策（propose 已确认）：
> - **范围调整**：T5 execve 注册（M1 批3 已做）/ T4 PF kill（M1 批4 推迟 M5）**不重做**；T6 fork VMA 复制放批4。
> - **匿名优先**（批1-3）；文件映射 `backing_inode` 批4 基础（真 Page Cache 留 M4）。
> - **syscall 返回 -errno**（A 翻译边界，`errno.hpp`）；`USER_MMAP_BASE/END`（M1 批2 [4GB,24GB)）mmap 用。
> **实机冒烟（批4）**：改 syscall 表 + fork VMA，启动路径，`timeout make run` 兜底。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `sys_mmap`（9）：匿名映射 + `find_free_area`/MAP_FIXED + VMA insert（懒分配）+ PROT/MAP 常量 + errno + 单测（set_current 模式） | ✅ | — | 716/0（+4） |
| 批2 | `sys_munmap`（11）：VMA `remove` 拆分 + 释放 demand-paged 物理页 + `unmap` + 单测 | ✅ | — | 719/0（+3） |
| 批3 | `sys_mprotect`（10）：VMA flags（保留 base 替换 R/W/X）+ PTE re-map + 单测 | ✅ | — | 721/0（+2） |
| 批4 | fork VMA 复制（T6，含 backing）+ 文件映射基础（fd→Inode backing，内容 M4）+ vma.hpp backing 修正 InodeOps*→Inode* + 收尾 + 实机冒烟 | ✅ | — | 721/0 + 实机不炸 |

**完成总结**（712→721，F2-M2 +9）：mmap 三 syscall 落地——`sys_mmap`（9，匿名/文件映射，懒分配 + `find_free_area`/MAP_FIXED + VMA insert）+ `sys_munmap`（11，VMA remove 拆分 + 释放 demand-paged 页 + unmap）+ `sys_mprotect`（10，保留 base 替换 R/W/X + PTE re-map）+ fork VMA 复制（CoW 页表后克隆父 VMA 含 backing）+ 文件映射基础（fd→Inode backing，内容 demand-read 留 M4）。架构：A 翻译边界（ErrorOr→errno，`errno.hpp`）；syscall handler 统一 6 参（SyscallFn）；VmaFlags 补 `operator&`（mprotect 提取 base）。关键校正：批1 VMA backing 用 `InodeOps*` 错（inode.hpp 是 `struct Inode` + `InodeOps` vtable），批4 改 `Inode*`。实机冒烟启动到 GUI 不炸。遗留：文件映射 demand-read 内容（M4 Page Cache）/ PF 真 segfault（M5）。

## OPEN GOTCHAS（跨里程碑通用，活警告）
1. **验证 target**：内核改动用 run-kernel-test（~694 项）；host 单测（`test/unit/`）不在其中，改被 mock 类后 push 前补全量编译（L5）。
2. **Cinux-Base 是子模块**：`Logger`/`LogLevel`/`RingBuffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`，复用勿重写。
3. **里程碑区分「类型就绪」vs「内核消费」**（M0/M1/M2 同构）：Cinux-Base 类型常先就绪，待办是 kernel/ 增量消费。
4. **klog_* 实时输出的 reentrancy guard**（批4a）：`g_klog_emit_depth` 让 klog sink 跳过 log 自身的 kprintf，避免同一行双重入队——是 kprintf→klog_* 迁移的前提（否则迁后丢 console）。定义须在 `log()` 前。
5. **崩溃 ring 读不到**：fatal_halt/kpanic 死循环，ring 历史崩溃后读不到；非崩溃异常(#DB/#BP/#GP-user)/error 进 ring 可 dmesg 读。kpanic/dump 保留 kprintf（实时诊断/halt）。
6. **kprintf 全量迁移未做**：294 个（除 mini 148）留渐进；mini 148 不迁（早期启动无 ring）。vkprintf_impl 第三参是 va_list 非 va_args，需 va_list 或手动 format。
7. **direct-map（phys+KERNEL_VMA）勿 unmap**（批2 教训，2026-06）：direct-map 是 phys↔virt 永久固定映射 + demand-paged，整个 kernel 共用。DMA/映射代码 free 时只回收 phys（`free_pages`），**绝不 `vmm.unmap(phys+KERNEL_VMA)`**——拆槽会让后续 demand paging 反复映射错 phys 死循环（QEMU 卡死，DmaPool 首版教训）。AHCI 同款（map 不 unmap）。free_page_count 因页表 demand-page 开销不精确对称，测试断方向不断绝对。
8. **QEMU AHCI 容量 ≠ ext2.img 文件大小**（M4 批2 教训，2026-06）：写高号 sector（如 7000，文件层面 8192 sector 内）仍 `[AHCI] command timeout`——QEMU AHCI IDENTIFY 报告的容量几何 < 文件大小，越界写不响应。真机写测试用已知可写的低 sector（`test_ahci_write` 的 sector 2000 = ext2 块 1000）+ 读原值/写回 restore，避免破坏 ext2（后续 ext2 套件同次运行依赖干净盘）。`AHCIBlockDevice::block_count()` 精确值待 F5-M1 ATA IDENTIFY。
9. **kernel freestanding 内存操作用 `kernel/lib/string.hpp`，禁 `<cstring>`/`<string.h>`**（M4 CI 教训，2026-06）：CI（Ubuntu glibc + 编译器 spec 默认 `_FORTIFY_SOURCE=2`）把 `<cstring>`/`<string.h>` 的 `memcpy`/`memset` 宏改写为 `__memcpy_chk`，但 kernel freestanding 不链 libc → `big_kernel_test`/host 链接 `undefined reference to __memcpy_chk`（PR #5 kernel-tests+host-tests Build 双炸）。本地无 FORTIFY spec 故未复现（GOTCHA #1 同类盲区：本地绿≠CI 绿）。一律用 `kernel/lib/string.hpp` 的 kernel 实现（非 fortify）；`memcmp` 不被 fortify 改写但为一致同源。
