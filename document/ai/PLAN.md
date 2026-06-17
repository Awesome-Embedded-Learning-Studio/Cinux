# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M3 = DMA 基础设施 ✅ 完成（2026-06-16）**。
> **F1-M4 = 块设备抽象 ✅ 完成（2026-06-16）**。
> **F5-M1 = AHCI DMA 迁移 ✅ 完成（2026-06-16）**。
> **F2-M6 = ext2 Cache ✅ 完成（2026-06-17）**。F2 进度 6/7（M1-M6 ✅），下一 **F2-M7 Buddy**（Slab → M7b）。
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

## ✅ F2-M1（VMA 区域记账）已完成 — 2026-06-16（PR #7 `a65d8ff` squash）

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
| 批1 | `vma.hpp/cpp`：`VmaFlags` + `VMA` 结构体 + `IVMAStore` 抽象 + `LinkedListVMAStore`（insert 有序+合并 / find / remove 拆分 / find_free_area）+ 单测 | ✅ | a65d8ff | 710/0（+5） |
| 批2 | `AddressSpace` 持 `LinkedListVMAStore`+`Spinlock` 成员（`vmas()`/`vma_lock()` 访问器，构造建/析构 RAII）+ `LinkedListVMAStore` 补 move（AddressSpace move-only 需成员可 move）+ `memory_layout` 加 `USER_BRK`/`MAP` 常量（按实际栈顶≈32GB 校正，非 todo 127TB） | ✅ | a65d8ff | 712/0（+2） |
| 批3 | execve ELF 段（PF_W/PF_X→VmaFlags）+ init/gui_init 用户栈（Stack flag）注册 VMA；insert 失败→路径失败保 VMA 完整 | ✅ | a65d8ff | 712/0（启动路径未被 run-kernel-test 执行，靠批4 实机） |
| 批4 | PF demand paging 加 VMA `find()` 诊断（未命中 klog_warn 但仍 demand page，不改行为；真 segfault 留 M5）+ 收尾 + 实机冒烟 | ✅ | a65d8ff | 712/0 + 实机启动不炸 |

**完成总结**（705→712，F2-M1 +7）：VMA 记账基础设施落地——`LinkedListVMAStore`（侵入式有序链表，insert 合并 / remove 拆分 / find / find_free_area，store-owns RAII）+ `IVMAStore` 抽象（可换红黑树）+ AddressSpace 集成（值成员 `vma_store_` + `Spinlock`，补 move）+ execve/栈注册（PF_W/PF_X→VmaFlags；Stack flag）+ PF demand paging VMA `find()` 诊断（未命中 warn 不改行为，真 segfault 留 M5）。架构：A.6 ErrorOr（逻辑错误）；A.7 不入 Cinux-Base（依赖 heap）；用户布局常量按实际栈顶≈32GB 校正（非 todo 草案 127TB）。关键教训：operator new 返 nullptr 非 panic（OOM 崩惯例）；klog_warn 是宏禁加命名空间前缀；启动路径不被 run-kernel-test 覆盖（靠实机冒烟）。遗留：PF 硬门控（M5）/ fork VMA 复制（F3）。

## ✅ F2-M2（mmap/munmap/mprotect）已完成 — 2026-06-17（PR #8 `d3b7cfa` squash）

> 目标：实现 mmap/munmap/mprotect syscall（Linux 9/11/10），消费 M1 VMA（`find_free_area`+`insert` / `remove` / flags），让用户程序动态内存映射。mmap 懒分配（仅建 VMA，PF 时 demand page，兼容 M1 批4 诊断）。
> 决策（propose 已确认）：
> - **范围调整**：T5 execve 注册（M1 批3 已做）/ T4 PF kill（M1 批4 推迟 M5）**不重做**；T6 fork VMA 复制放批4。
> - **匿名优先**（批1-3）；文件映射 `backing_inode` 批4 基础（真 Page Cache 留 M4）。
> - **syscall 返回 -errno**（A 翻译边界，`errno.hpp`）；`USER_MMAP_BASE/END`（M1 批2 [4GB,24GB)）mmap 用。
> **实机冒烟（批4）**：改 syscall 表 + fork VMA，启动路径，`timeout make run` 兜底。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `sys_mmap`（9）：匿名映射 + `find_free_area`/MAP_FIXED + VMA insert（懒分配）+ PROT/MAP 常量 + errno + 单测（set_current 模式） | ✅ | d3b7cfa | 716/0（+4） |
| 批2 | `sys_munmap`（11）：VMA `remove` 拆分 + 释放 demand-paged 物理页 + `unmap` + 单测 | ✅ | d3b7cfa | 719/0（+3） |
| 批3 | `sys_mprotect`（10）：VMA flags（保留 base 替换 R/W/X）+ PTE re-map + 单测 | ✅ | d3b7cfa | 721/0（+2） |
| 批4 | fork VMA 复制（T6，含 backing）+ 文件映射基础（fd→Inode backing，内容 M4）+ vma.hpp backing 修正 InodeOps*→Inode* + 收尾 + 实机冒烟 | ✅ | d3b7cfa | 721/0 + 实机不炸 |

**完成总结**（712→721，F2-M2 +9）：mmap 三 syscall 落地——`sys_mmap`（9，匿名/文件映射，懒分配 + `find_free_area`/MAP_FIXED + VMA insert）+ `sys_munmap`（11，VMA remove 拆分 + 释放 demand-paged 页 + unmap）+ `sys_mprotect`（10，保留 base 替换 R/W/X + PTE re-map）+ fork VMA 复制（CoW 页表后克隆父 VMA 含 backing）+ 文件映射基础（fd→Inode backing，内容 demand-read 留 M4）。架构：A 翻译边界（ErrorOr→errno，`errno.hpp`）；syscall handler 统一 6 参（SyscallFn）；VmaFlags 补 `operator&`（mprotect 提取 base）。关键校正：批1 VMA backing 用 `InodeOps*` 错（inode.hpp 是 `struct Inode` + `InodeOps` vtable），批4 改 `Inode*`。实机冒烟启动到 GUI 不炸。遗留：文件映射 demand-read 内容（M4 Page Cache）/ PF 真 segfault（M5）。

## ✅ F2-M3（brk）已完成 — 2026-06-17（PR #9 `4331853` squash）

> 目标：`sys_brk`（Linux 12）用户态堆（malloc 底层）。**懒 brk**：调 `brk_current`（边界检查），不 map/unmap 页，堆区访问 PF 时 demand page（和 M2 mmap 懒分配一致，复用 M1 批4 诊断）。
> 决策：懒 brk（非 todo eager map，与 M2 统一）；Task 加 `brk_current`/`brk_initial`/`brk_max`；execve 设 `brk_initial`（ELF 段末尾）+ Heap VMA `[brk_initial, USER_BRK_MAX)`。
> 不做：`user/test_brk.c` + sbrk libc wrapper（留后续）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | Task brk 字段 + `sys_brk`（12，懒：addr==0 返当前 / 越界返当前 / 否则调 brk_current）+ 注册 + 单测 | ✅ | 4331853 | 722/0（+1） |
| 批2 | execve 设 `brk_initial`（ELF 段末尾）+ Heap VMA `[brk_initial, USER_BRK_MAX)` + 收尾 + 实机冒烟 | ✅ | 4331853 | 722/0 + 实机不炸 |

**完成总结**（721→722，F2-M3 +1）：brk 落地——`sys_brk`（12，懒：调 `brk_current`，边界 `[brk_initial, brk_max]`，不 map/unmap，PF demand page）+ Task `brk_current`/`brk_initial`/`brk_max` 字段 + execve 设 `brk_initial`（ELF 段末尾页对齐）+ Heap VMA `[brk_initial, USER_BRK_MAX)`。架构：**懒 brk**（与 mmap 统一，复用 demand paging）；syscall handler 6 参；brk 不返 errno（返地址，Linux 语义）。实机冒烟启动到 GUI 不炸。遗留：`user/test_brk.c` + sbrk libc wrapper（用户程序实际用 brk 时加）。

## ✅ F2-M4（Page Cache）已完成 — 2026-06-17

> 目标：内核级 Page Cache，让 **file-backed mmap 的 demand paging 读到真文件内容**（M2 批4 留的洞：[sys_mmap.cpp:128-134](kernel/syscall/sys_mmap.cpp#L128-L134) 只记 `backing` inode，PF 时 [exception_handlers.cpp:269](kernel/arch/x86_64/exception_handlers.cpp#L269) 一律映射零页 → 文件映射读到全零）。缓存键 = `(Inode*, page_offset)`。
> 决策（propose 已确认）：
> - **#1 最小 MVP**：cache + file-mmap demand-read（读路径）。脏页写回 / MAP_SHARED 写一致性、全 `read()` 经缓存、LRU+跨进程共享+CoW **留后续**。
> - **#2 从干净 main 开**（M4⊥brk(M3)；侦察期间 PR #9 M3-brk 已合入 main，M4 分支天然含 M3）。
> - **#3 复用 direct-map**：缓存页 virt = `phys + KERNEL_VMA`（免 temp-map、不 unmap，GOTCHA #7 同 M3 DmaPool）。
> - **#4 读在锁外、insert 在锁内**：PF handler 跑 IF=0，`get_page` 先把文件内容读到已 present 的 direct-map 页（锁外，AHCI 轮询 IF=0 成立），再短临界区 insert（irq-guard Spinlock），杜绝 IO-under-lock 重入死锁 → 新 GOTCHA。
> - **PTE 权限按 VmaFlags 翻译**（Write→WRITABLE、!Exec→NX；现在硬写 WRITABLE，批2 修正）。
> 依赖就绪：M1 VMA（`backing`/`file_offset`/`VmaFlags`）/ M2 mmap 文件 backing / ErrorOr / PMM `alloc_page_locked` / VMM `map_nolock` / `Ext2FileOps::read(inode,offset,buf,count)`。批1 待核对 `Inode` 经 `ops->read` 暴露 read。
> 不做：脏页写回（MAP_SHARED 写一致性）/ 全 `read()` 经缓存（→M6 ext2 Cache）/ LRU 淘汰 / 跨进程共享缓存 + CoW-for-shared-file（→F3/M5）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `page_cache.hpp/cpp`（`CachedPage`+`PageCache` 256-bucket hash，`lookup`/`get_page` 填充+EOF 零填/`release`/stats，`ErrorOr`，irq-guard Spinlock，direct-map virt）+ fake `InodeOps` mock + `test_page_cache.cpp` 单测（命中/未命中填充/refcount/同 inode+offset 二次命中/EOF 零填） | ✅ | db42957 | 728/0（+6） |
| 批2 | `handle_pf` 文件感知（file-backed VMA→`get_page`→`map_nolock`；PTE Write→WRITABLE，NX 留 F9 NXE；读锁外 insert 锁内）+ `sys_mmap` offset 页对齐校验 + mprotect 移除 NX（NXE 未启用保留位）+ 匿名路径不变 | ✅ | db42957 | 728/0（回归；文件路径单测 dormant，批3 Test B 锻炼） |
| 批3 | 真文件 mmap 闭环（`test_file_mmap.cpp`：Test A `get_page` 真盘读 ext2 字节比对 + cache hit；Test B 文件 VMA `as.activate()` 后访存经 #PF→handle_pf 文件路径→字节比对，端到端验证 wiring）+ NX 修复 + 收尾 | ✅ | db42957 | 730/0（+2） |

**完成总结**（722→730，F2-M4 +8）：Page Cache 落地——`CachedPage` + `PageCache`（256-bucket hash，`(Inode*, page_offset)` 键，direct-map virt = `phys + KERNEL_VMA`，ref_count，无淘汰）+ `get_page`（命中 bump ref / 未命中 alloc 页 → 锁外 `inode->ops->read` 填充 + EOF 零填 → 锁内 insert，IF=0 安全）+ `lookup`/`release`/stats + `handle_pf` 文件感知（file-backed VMA → `get_page` → `map_nolock`，PTE Write→WRITABLE，NX 留 F9 NXE；匿名路径不变；execve ELF 段是匿名故 boot 走匿名）+ `sys_mmap` offset 页对齐校验。验证：批3 `test_file_mmap` Test A（cache 真盘读 ext2 字节比对 + cache hit）+ Test B（文件 VMA `as.activate()` 后访存经 #PF→handle_pf 文件路径→字节比对，端到端验证 wiring）。架构：A.6 ErrorOr（`get_page`→`ErrorOr<CachedPage*>`）；A.7 不入 Cinux-Base（依赖 heap/PMM/Inode）；复用 direct-map（GOTCHA #7 同 DmaPool，不 temp-map 不 unmap）。关键教训 GOTCHA #10（NXE 未启用→NX 保留位，Test B PF round-trip 定位）。MVP 只做读路径：脏页写回 / MAP_SHARED 写一致性 / 全 `read()` 经缓存 / LRU+跨进程共享+CoW 留后续（M6/F3/M5）。

## ✅ F2-M5（Demand Paging 硬门控）已完成 — 2026-06-17

> 目标：把 PF handler「VMA 未命中→映射零页容错」（[exception_handlers.cpp:258-271](kernel/arch/x86_64/exception_handlers.cpp#L258-L271)）升级为 **VMA 硬门控**——用户态 not-present PF 无 VMA 命中 → 真 segfault（终止进程），兑现 M1 记账价值。命中合法 VMA → 照常 demand page（匿名零页 / 文件 page cache，M4 路径不变）。
> 决策（propose 已确认，2026-06-17）：
> - **#1 segfault 终止**：批1 两种方式都 spike（直接 `exit_current()` vs 标记 Dead+延迟退出）再定，按 IF=0 中断栈 task switch 语义选稳的。
> - **#2 栈增长**：Stack VMA 扩到 ~1MB 向下 demand-page 自动增长 + 栈底 guard page（溢出→segfault），现有程序不崩。
> - **#3 权限门控范围**：只做「无 VMA→segfault」；写只读/执行权限违规留后续（NX 因 NXE 未启用留 F9，GOTCHA #10）。
> 终止机制复用 [scheduler.cpp:179-200](kernel/proc/scheduler.cpp#L179-L200) `Scheduler::exit_current()`（F3 信号未做，临时等价 SIGSEGV-killed）。
> 依赖就绪：M1 VMA find / M2 mmap / M3 brk / M4 page cache / fork CoW（present 分支不受 not-present 门控影响）。
> 不做：SIGSEGV 信号交付（F3）/ NX 强制（F9）/ 栈 ulimit / page cache 跨进程 CoW（F3）/ mmap 脏页写回（后续）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | spike segfault 终止（`exit_current` 在 PF handler IF=0 中断栈可行性）+ handle_pf not-present user 硬门控（无 VMA→segfault；命中→demand 不变）+ 单测 | ✅ | 02e22c2 | 730/0（回归） |
| 批2 | Stack VMA 扩 ~1MB 自动增长 + 栈底 guard page（溢出→segfault） | ✅ | 652f698 | 730/0（回归） |
| 批3 | 全路径回归（execve ELF/brk/mmap/fork CoW 硬门控下合法放行）+ 修漏注册 VMA + 实机冒烟迭代 | ✅ | —（纯验证，无代码改动） | 730/0 + GUI 实机不炸 |
| 批4 | 收尾 + 全量 run-kernel-test + 实机冒烟（init/gui/shell 不崩=成败判据）+ ROADMAP/PLAN/todo/notes + GOTCHA（PF-exit 约束/栈增长/NXE 留 F9） | ✅ | (本次) | 730/0 + 实机不炸 |

**完成总结**（730→730，F2-M5 门控+栈增长，测试数不变）：demand paging 硬门控落地——`handle_pf` not-present user PF，`vma==nullptr` 时真实 user-mode fault（`err&0x04`）→ `klog_error` + `Scheduler::exit_current()` 终止进程（SIGSEGV 等价，F3 信号前临时方案；`context_switch` 抛弃 prev 中断帧切 next，不返回此处）；kernel-mode 访问用户地址（ring0 test / `copy_to_from_user`，`!(err&0x04)`）保持零页容错（不误杀 kernel-test PF 注入）。命中合法 VMA → demand 不变（匿名零页 / M4 文件 page cache）。栈增长：`USER_STACK_GROWTH=1MB` 常量（[usermode.hpp](kernel/arch/x86_64/usermode.hpp)），init.cpp/gui_init.cpp Stack VMA 扩到 `[TOP-1MB, TOP)`（仅顶 16KB 预分配，余 demand 增长），VMA 底外→segfault（guard）。架构：user-mode 判定靠 `err&0x04`（test 豁免天然分界，无需新 ErrorOr，复用 `exit_current`）。关键教训 GOTCHA #11（PF 硬门控 user-mode 判定 + 栈增长配套 + execve 复用 AS 不清 VMA）。验证：730/0 回归（kernel-mode 路径）+ `make run` GUI 启动到桌面不崩（user-mode 路径，无 segfault/panic）+ 全路径代码分析（所有合法 demand PF 有 VMA 覆盖）。遗留：SIGSEGV 信号（F3）/ NX 强制（F9）/ 权限违规门控（写只读）/ segfault 进程资源清理（`exit_current` leak，待 task exit cleanup）。

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
10. **EFER.NXE 未启用 → NX bit（bit63）是保留位**（F2-M4 批3 教训，2026-06）：F9 NX/SMEP/SMAP 未做，NXE 关闭。PTE 设 `FLAG_NX`（bit63）后访问该页触发 **reserved-bit #PF（err=0x8）循环**——`handle_pf` 文件路径初版给非 exec 页设 NX，致 Test B PF round-trip 无限循环。诊断：PF handler 临时打印 err/cr3/asPml4/translate，err=0x8(RSVD) + translate==phys 确认 PTE 设对、是保留位违例。修复：`handle_pf` 文件路径 + `sys_mprotect` 都**暂不设 NX**（非 exec 页不强 NX），留 F9 启用 NXE 后再开。另：单测验 `handle_pf` 文件 PF 需 `as.activate()` 切到进程 PML4 再访存（boot PML4 user 半空）；`get_page` 读文件在 cache 锁外、insert 在锁内（IF=0 防 IO-under-lock 重入死锁）；缓存页 virt 复用 direct-map（GOTCHA #7 同 DmaPool）。
11. **PF handler 硬门控的 user-mode 判定 + 栈增长配套**（F2-M5 教训，2026-06）：`handle_pf` 杀进程（`exit_current`）必须判 `err & 0x04`（真实 user-mode fault）——kernel-test 是 ring0 访问用户地址（`err&0x04=0`），误杀会让测试 hang（test_file_mmap Test B 模式：`CurrentTaskGuard` 设 tmp task 但 ring0 访存）。`exit_current` 的 `context_switch` 抛弃 prev 中断帧切 next，不返回 PF handler（segfault 终止可行；"标记 Dead+延迟退出"反而要伪造中断帧，更复杂）。**栈 VMA 必须与硬门控配套**：门控上了但栈仍固定 16KB → 深调用栈用户程序栈 PF 落 VMA 外 → segfault（run-kernel-test 不跑真实用户深栈故绿，掩盖此依赖；init.cpp/gui_init.cpp 用 `USER_STACK_GROWTH=1MB` 扩 Stack VMA）。**execve `clear_user_mappings` 只清页表不清 VMA store**——Stack VMA 经 fork 继承 / execve 复用 AS 保留传播到所有用户程序。run-kernel-test 全 kernel-mode fault，不覆盖 user-mode segfault/demand 路径（靠 `make run` 实机 + `test_shell_*` 间接）。
12. **read() 经 PageCache 的递归规避 + pipe 判别**（F2-M6 教训，2026-06）：`read_bytes` 是**新函数**（sys_read 对 `is_page_cacheable()` 真者调它），其内部 `get_page` 填充走 `inode->ops->read`（Ext2FileOps 读盘原语）——`Ext2FileOps::read` **不改不调 page_cache**，否则 read→get_page→read 死循环。判别"磁盘文件 vs pipe"不能用 `inode->type`（pipe 的 type 也是 `Regular`，[test_sys_pipe.cpp:105]），禁 RTTI 无法 dynamic_cast；用 `InodeOps::is_page_cacheable()` virtual（默认 false，Ext2FileOps override true）。`g_page_cache.hit_count()` 是 boot 全局跨测试累积，断言"二读命中"须在二读前捕获基线。

## ✅ F2-M6（ext2 Cache）已完成 — 2026-06-17

> 目标：`sys_read` 对磁盘文件走 PageCache，与 demand paging 共用 `(Inode*, page_offset)` 缓存——重复读命中免读盘，闭环读路径唯一缓存层。M4 PageCache 此前只服务 file-backed mmap 的 PF 路径，read() 直走 Ext2FileOps 每块读盘无缓存。
> 决策（propose 已确认）：
> - **#1 read() 走 read_bytes**（复用 M4 get_page，不另起缓存逻辑）。
> - **#2 判别用 virtual `is_page_cacheable()`**（pipe type 也是 Regular，禁 RTTI；默认 false，Ext2FileOps override true，源码兼容现有 mock）。
> - **#3 read_bytes 切片按页对齐**，EOF 以 `inode->size` 截断（ext2 lookup 已填），partial-read 中途失败回已读量。
> 不做：脏页写回 / MAP_SHARED 写一致性 / LRU 淘汰 / 跨进程 CoW（留后续/F3）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `PageCache::read_bytes`（按页切片经 get_page + EOF 截断）+ `InodeOps::is_page_cacheable()` virtual（默认 false）+ Ext2FileOps override true + test_page_cache 3 测（基本+EOF / 二读命中缓存 / 跨页裁剪） | ✅ | ca13352 | 733/0（+3） |
| 批2 | `sys_read` 分流（is_page_cacheable 真→read_bytes，否则原 read；pipe/ramdisk 不变）+ test_syscall_ext2 端到端测（真 AHCI/ext2 读 /hello.txt 两遍 hit_count 升+字节一致） | ✅ | 3a24439 | 734/0（+1） |
| 收尾 | 文档(本文+ROADMAP+todo) + 全量 run-kernel-test + host test_host + 实机冒烟 | ✅ | (本次) | 734/0 + host 49/0 + 实机不炸 |

**完成总结**（730→734，F2-M6 +4）：read() 缓存路径落地——`PageCache::read_bytes(inode,off,buf,count)`（按页对齐切片复用 M4 `get_page`：命中免读盘 / 未命中填充+EOF 零填，EOF 以 `inode->size` 截断，partial-read 回已读量）+ `InodeOps::is_page_cacheable()` virtual（默认 false，Ext2FileOps override true；pipe type 也 Regular 故 type 不可靠，禁 RTTI 用 virtual 判别）+ `sys_read` 分流（cacheable→read_bytes，否则原 inode->ops->read，pipe/ramdisk 不变）。架构：A.6 ErrorOr；避免递归（read_bytes 新函数，Ext2FileOps::read 读盘原语不改）；翻译边界 errno 不变。验证：批1 单测（read_bytes 缓存机制）+ 批2 真机端到端（AHCI/ext2 读两遍 hit_count 升 = sys_read→read_bytes 接线铁证）+ host test_host 49/0（InodeOps virtual 源码兼容）+ 生产内核启动到 GUI 桌面不炸。遗留：脏页写回 / MAP_SHARED 写一致性 / LRU 淘汰 / 跨进程 CoW 留后续（F3）。
