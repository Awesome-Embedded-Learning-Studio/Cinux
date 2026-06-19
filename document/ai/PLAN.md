# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M3 = DMA 基础设施 ✅ 完成（2026-06-16）**。
> **F1-M4 = 块设备抽象 ✅ 完成（2026-06-16）**。
> **F5-M1 = AHCI DMA 迁移 ✅ 完成（2026-06-16）**。
> **F2-M6 = ext2 Cache ✅ 完成（2026-06-17）**。
> **F2-M7 Buddy PMM ✅ 完成（2026-06-18，fresh KVM 742/0 + GUI 冒烟）**：buddy 伙伴系统替换 PMM flat bitmap（per-order bitmap free-list，非侵入式）。Bug1（direct-map reserved PF，批3）+ Bug2（WSL2 nested KVM 对侵入式 free-list 写读不一致，改 bitmap 解，GOTCHA#14）均修。**详见下方「F2-M7 Buddy PMM」段 + `document/notes/2026-06-17-f2-m7-direct-map-buddy-handoff.md`**。solid 基线 = main（M6 #12，734）。F2 进度 7/7 + M7b（M1-M7 ✅，**M7b SLAB ✅ 完成 2026-06-18**：kmalloc 全替 Heap + 专用缓存 Task/VMA/CachedPage，见下方「F2-M7b」段）。
> **FO 可观测性/调试基建 ✅ 完成（2026-06-18，763/0 + panic 冒烟）**：frame pointer + KALLSYMS lookup + 防御 backtrace + 统一 panic handler（收编 dump_registers/kpanic/fatal_halt + backtrace + memstats）+ dump_memory_stats。关键 GOTCHA：`CMAKE_BUILD_TYPE` 默认空(-O0)→ 首次 -O2 Release 验证全绿（建议 CI 加 -O2 门禁）；`VMM::translate()` 不支持 huge 页 → backtrace 改栈范围检查；kprintf 不支持 `%zu`。**M5 崩溃持久化推迟**（持久化层前提不满足）、**1b 真实符号注入 follow-up**（CMake 两阶段，裸地址+addr2line 降级）。详见 `document/notes/2026-06-18-fo-observability.md`。
> **F3-M1 信号系统 ✅ 完成（2026-06-18，5 批，763→783）**：核心 POSIX 信号（Signal/SigSet/SigAction）+ 投递（send/pick/check_and_deliver）+ kill/sigaction/sigprocmask/sigreturn + Custom handler round-trip（中断路径 + int $0x80 trampoline）+ 集成（PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE）。详见下方「F3-M1 信号」段 + `document/notes/2026-06-18-f3-m1-signals.md`。
> **F3-M2 线程支持（clone + futex + TLS）✅ 完成（2026-06-18，5 批，783→810）**：为 musl/pthread 打内核地基。①TLS(fs_base) ②futex(WAIT/WAKE/BITSET) ③共享 refcount 指针化(sig_actions/fd_table/cwd)+retrofit fork ④线程组+clone 核心(子进程用户栈返回 patch 帧 user_rsp,GOTCHA#18) ⑤cleartid exit 集成+libc wrapper。**关键踩坑 GOTCHA#17-20**（FS_BASE 规范地址/clone 用户栈返回/改接口致测试早返回悬垂/栈拷贝 full_used 下溢）。**真用户态线程 round-trip + 实机 GUI 冒烟 + AddressSpace refcount + futex timeout 留 follow-up**。详见文末「✅ F3-M2」段 + 各批 notes。下个焦点：F3-M3 进程组/会话（已启动，见下）。
> **F3-M3 进程组/会话 + waitpid 阻塞 ✅ 完成（2026-06-19，5 批，810→827）**：为 Job Control / TTY 打地基 —— pgid/sid/setpgid/setsid/killpg + 补 waitpid 阻塞（复用 futex 的 block/unblock + exit 唤醒父）。详见文末「✅ F3-M3」段。
> **F3-M4 调度器接口验证与增强 ✅ 完成（2026-06-19，5 批，827→840）**：①SchedulingClass 策略钩子(task_tick/task_fork/task_deadline,时间片抢占内聚到调度类,删 current_slice_) ②优先级感知 RoundRobin(pick_next 选 priority 最小者,同优先级 RR) ③多调度类实际查询(pick_next_from 数组原语,schedule/exit_current/run_first 不再绕过 classes_[]) ④SIGSTOP/SIGCONT 真调度(TaskState::Stopped 状态机 + signal_send 发送时恢复 + schedule 守卫排除 Stopped)。**向后兼容**(生产单类场景等价,827 回归全绿)。关键踩坑 GOTCHA#22(TaskBuilder 消耗全局 tid 计数器跨测污染)。**F3 进程与线程全里程碑收官(M1-M4)**。详见文末「✅ F3-M4」段 + `document/notes/2026-06-19-f3-m4-{1,2,3,4}-*.md`。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

> **F-INFRA 基建加固 🔄 进行中（2026-06-19 起）**：F2/F3 后复杂度陡增（F4 SMP + 网络）前夯基——静态检查/指针语义/调试基建/CI 粘合，10 批。详见下方「🔄 F-INFRA」段。

## 🔄 F-INFRA（基建加固）进行中 — 2026-06-19 起

> 横切里程碑（像 FO，插 F4 SMP 前）。目标：把调试/静态检查/指针语义/CI 粘合从"靠自觉"升级为"机器可见 + CI 强制"，让 UB/悬垂指针/并发死锁/隐式窄化在非确定性到来前被抓住。对齐用户铁律"可调试优先于性能"。
> 来源：2026-06-19 一个 26-agent workflow（6 维代码审计 + 5 维联网调研 + 综合 + 对抗验证 + 完整性审查）的验证结论；记忆 `infra-hardening-investigation.md`。
> 约束：C++17 freestanding/无异常/无 RTTI；Cinux-Base 零堆/零 OS 耦合；内核单核（并发真修复 R3 原子引用计数、R6-Part2 锁序图 DFS 划 **F4-M5**）。基线 840/0（F3-M4 收官，PR#19 合 main）。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；改公共接口/InodeOps/mirror 补 `cmake --build build -j$(nproc)` 全量（CI 盲区：run-kernel-test 不编 test/unit/）。

| 批 | Tier | 范围 | 状态 | Commit | 测试 |
|----|------|------|------|--------|------|
| I-1 | 0 | CI 防挂死（timeout 包裹 run-kernel-test）+ 失败上传串口日志（G1/G8） | ✅ | (本次) | 840/0 |
| I-2 | 0 | check_freestanding_headers.py + 修 icon_data.hpp `<array>` + CMake GCC 版本断言（G9/G2） | ✅ | (本次) | 840/0 |
| I-3 | 0 | 警告标志收紧（-Wshadow/-Wold-style-cast/-Wnon-virtual-dtor/-Woverloaded-virtual/-Wformat=2 + -Werror=return-type）+ 清理（17 cast + 15 预存警告 + build-id）→ 零警告构建（R2） | ✅ | (本次) | 840/0 |
| I-3b | 0 | kprintf/kvprintf/kpanic 加 `__attribute__((format))` + 清理暴露的不匹配（R2b） | ⏳ | — | — |
| I-4 | 0 | static_assert 布局锁：SlabHeader/SlabCache/LogEntry/VMA + 两处 InterruptFrame offsetof 矩阵（R11） | ⏳ | — | — |
| I-5 | 1 | KALLSYMS 真符号注入：nm 两趟链接（big_kernel + big_kernel_test）+ main.cpp 注册（R4） | ⏳ | — | — |
| I-6 | 1 | .gdbinit 64 位长模式重写（无偏移 file big/big_kernel）+ decode-trace.sh addr2line 一键（R9/G5） | ⏳ | — | — |
| I-7 | 2 | not_null<T> 进 Cinux-Base（精简 gsl，裸 assert 不耦合 kpanic，仿 optional.hpp）+ scheduler 永不为 null 入参采纳（R5） | ⏳ | — | — |
| I-8 | 2 | .clang-tidy 精选 allowlist + advisory（非阻塞）CI job，版本锁定（R8） | ⏳ | — | — |
| I-9 | 3 | UBSAN freestanding 桩：CINUX_UBSAN（Debug），GCC libubsan 规范签名（不抄 SerenityOS），桩调 kpanic，Cinux-Base/panic/kprintf/backtrace 排除插桩（R1） | ⏳ | — | — |
| I-10 | 3 | lockdep-Part1：held_spinlock_depth 计数 + schedule/block 断言为 0 + panic 重入标志防自死锁（R6） | ⏳ | — | — |

划归 F4-M5（不在本里程碑）：R3 原子引用计数（SharedCwd/SharedSigActions）、R6-Part2 锁序图 DFS。
follow-up（渐进，不拆批）：R7 BUG_ON/WARN_ON + CODING-TASTE 补 assert-vs-Error 判据、R10 mini-KASAN 红区、R12 next_tid 测试复位、R13 -O0 CI 矩阵、G3 确定性种子、G4 xfail 标记、G7 分层 include 检查、G10 启动阶段计时。

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
13. **direct-map 独立窗口 + KERNEL_VMA 重载区分**（F2-M7 direct-map 前置教训，2026-06）：`phys_to_virt` 用 `DIRECT_MAP_BASE=0xFFFF880000000000`（PML4[272]，loader 1GB/2MB 大页 identity 映全 RAM），**不是** KERNEL_VMA。KERNEL_VMA 窗口硬限 2GB 且 boot 只映 higher-half 0-1GB，phys>1GB 落未映射处 demand-page 非 identity（latent bug，buddy 侵入式链表写 high phys 触发 PF 重入踩烂 → 死循环）。`-cpu max` 仅 KVM 时设（qemu.cmake），无 KVM 落回 qemu64 不暴露 PDPE1GB → direct_map_up_to 必有 2MB 页 fallback。**迁移严判**：访问任意 PMM 页（页表/DMA/缓存页/GS）是 direct-map→DIRECT_MAP_BASE；kernel image 相对（pmm bitmap `__kernel_stack_top - KERNEL_VMA`、kernel 链接基址、boot higher-half PT）保留 KERNEL_VMA。direct-map PT 页放 [0x10000,0x20000)（<1MB 不过 PMM，持久）。**direct-map 区域（PML4[272]，`DIRECT_MAP_BASE+…`）严禁 `VMM.map/unmap/translate`**：direct-map 是 loader 建的 1GB/2MB huge 永久 identity 映射，全栈共享。`VMM::map` 内 `walk_level` 遇 huge entry（PS bit）会 **split**（拆成 4KB PT + 改写原 entry）；对 direct-map 的 1GB huge 触发即破坏全局 direct-map → 后续 `phys_to_virt` walk 错乱命中 phys 0 BIOS 数据当 PT → reserved-bit PF（err=0x9）。教训：M3 `DmaPool::alloc` 旧逻辑对 virt 调 `g_vmm.map`（M3 时 virt 在 KERNEL_VMA 窗口，map 无害），批2 迁 direct-map 后漏删 map → 首次 AHCI `g_dma_pool.alloc` 即崩（`test_cache_reads_real_file`，fresh build 才暴露）。修法：direct-map 复用 loader 永久映射——alloc 只取 phys（virt=phys+DIRECT_MAP_BASE 直接可用，**不 map**），free 只 free phys（**不 unmap**）。`VMM.translate` 对 huge 也不支持（walk_level huge+!alloc 返 nullptr），故 direct-map virt 不该经 translate 验证。
14. **WSL2 nested KVM（AMD）EPT 对侵入式 free-list 写读不一致**（F2-M7 Bug2 教训，2026-06-18）：buddy 初版侵入式 free-list 把 `next` 指针写在 free 页头（经 direct-map `phys+DIRECT_MAP_BASE`），依赖 direct-map 写读严格一致。WSL2 nested KVM（AMD `-accel kvm -cpu max`，hypervisor flag=14）EPT 对「huge page 内 sub-page 写」做不到一致——同地址 `0xFFFF880040000000`（phys 1GB）main 单次读返 valid（260096）、buddy op 读返 poison（`0xCAFEBABEDEADC0DE`），振荡→`pop_free` 遍历 #GP（`test_wm_close_button_closes_terminal_pipes`）。**TCG（`CINUX_NO_KVM=1`，2MB path）742/0 全绿**证明 buddy wiring 本身正确（根因 nested KVM 物理层，非逻辑 bug）。**修 = buddy 改非侵入式 per-order bitmap free-list**（bitmap 存 metadata 区，不写 free 页，KVM nested safe；`find_first_set` 天然 low-first）。诊断教训：CHK（main 单次读）valid ↔ buddy op 读 poison 的**同地址振荡**→ nested KVM EPT 嫌疑；**TCG 对比（`CINUX_NO_KVM` env）是定位物理层 bug 的利器**（逻辑层错 TCG 也复现，物理层错 TCG 绿）。凡依赖 direct-map sub-page 写读一致的侵入式结构（free-list/对象头）在 nested KVM 都有此风险，优先用 metadata 数组。
15. **slab 复用暴露按指针键控的缓存（F2-M7b 批2 教训，2026-06-18）**：page cache 原按 `Inode*` 指针做 hash/lookup 键。slab（正确）复用已释放 Inode 的内存地址给新 Inode → 新文件查 cache 时**命中陈旧页**（旧文件内容），`sys_read` 返回错字节（`test_shell_write` echo-redirect 读回 `"Hello from e"` 而非 `"hello world\n"`；Heap first-fit 侥幸不复用同地址故潜伏）。**根因非 slab**（复用是 slab 本职），是 page cache 用可复用指针当稳定键。修：cache 改按 `inode->ino`（稳定号）键控（hash/lookup/insert）。**通用铁律**：任何按对象指针/地址键控的在线结构（cache/table），当对象经 slab/heap 分配释放时都有同款陈旧命中风险，键须用稳定标识（id/number）而非指针。
16. **sigreturn 栈注入 trampoline 依赖 NXE 关闭 + Custom 走中断路径（F3-M1 教训，2026-06-18）**：Custom handler 的 sigreturn trampoline 是栈上 `int $0x80`（cd 80）代码，handler 返回地址指向它 —— 依赖栈页可执行。NXE 未启用（GOTCHA#10）故可行。**F9 启用 NXE 后栈不可执行，trampoline 失效，须迁 vdso/独立可执行页**。另：`syscall.S` 只保存精简帧（user_rsp/rip/rflags + 6 参 + rbx/rbp，**无 R12-R15**），sigreturn 经 syscall 无法完整恢复用户上下文 —— 故 Custom signal 投递挂**中断/异常返回路径**（ISR 宏 `call handler` 后 `signal_check_deliver_isr`），sigreturn 经 **IDT vector 0x80 trap gate（DPL=3）** 收完整 InterruptFrame 恢复；syscall 路径（`signal_check_and_deliver`）只投递 Default/Ignore，Custom 留 pending 给下次 IRQ0（时钟，延迟可忽略）。`signal_check_deliver_isr` 严判 `frame->cs & 0x03`（只用户态投递）—— kernel-test 全 ring0 中断点 skip，保护测试设的 pending 栈 task 不被误投递（exit_current 切走栈 task 崩）。
17. **wrmsr FS_BASE/GS_BASE 须规范地址，否则 #GP err=0（F3-M2 批1 TLS 教训，2026-06-18）**：x86-64 要求 FS_BASE/GS_BASE 段基址 MSR（`0xC0000100`/`0xC0000101`）持有**规范地址**（bits 48..63 须符号扩展 bit 47：低半 `0x0000...` / 高半 `0xFFFF...`）。写**非规范**值（如测试的 `0x1234567890ABCDEF`，bit 47=0 但 bit 48..63 非全 0）→ wrmsr 立刻 `#GP` err=0。诊断关键：panic 现场 RAX/RDX/RCX 全是 wrmsr 操作数（未进下条指令）→ 故障钉死在 wrmsr 本身 → 排除操作数错误即「MSR 不接受该值」→ 规范地址约束。真实 TLS 基址是用户指针（天然规范），clone(CLONE_SETTLS)/arch_prctl 不受影响——**主要坑测试与未来任何手写 FS/GS base 的代码**。`tls.hpp` 文档已注明。**此坑暴露 timeout 40 的价值**（panic 死循环挂死终端，timeout 杀进程暴露现场才定位，→ DIRECTIVES L5 补 timeout 40）。
18. **clone 子进程用户栈返回 = patch 帧 user_rsp 槽（F3-M2 批4 教训，2026-06-18）**：clone 子进程要以调用者 `stack` 参数返回用户态。syscall_entry 从 `%gs:0`(=kernel_stack_top) 载核栈，pt_regs 帧（96B）固定在 `[kernel_stack_top-96, kernel_stack_top)`，**user_rsp 在帧 offset 0 = `kernel_stack_top-96`**。clone 复用 fork 的拷核栈机制，拷完后直接 `*(uint64_t*)(child->kernel_stack_top-96) = stack;` patch——**无需从 current_rsp 算偏移**（帧在栈顶固定位置）。子进程经 fork_child_trampoline(rax=0) 解卷回 syscall_entry → SYSRET（user_rsp=stack, rax=0）。详见 `document/notes/2026-06-18-f3-m2-clone.md`。
19. **改公共接口后，依赖它的测试断言由过变挂 → TEST_ASSERT 早 return 跳过清理 → 悬垂状态污染后续测试（F3-M2 批4 教训，2026-06-18）**：`TEST_ASSERT` 失败时 `return`（早返回）。若测试末尾有清理（如 `set_current(prev)` 还原 current），断言失败会跳过它 → `Scheduler::current_` 悬垂指向已销毁的栈 `Task tmp` → 后续测试读悬垂 current 崩。批4 改 `sys_getpid` 返 `tgid`（线程共享进程身份）使既有 `test_sys_getpid` 断言失败（设 `tmp.pid` 未设 `tmp.tgid`）→ 早返回 → current_ 悬垂 → 远处的 vfs `Spinlock::acquire` 崩（current_→垃圾 task→fd_table 垃圾），**表象远离真因，极具迷惑性**（像内存踩踏，实为悬垂指针）。诊断：二分（git stash 隔离批）+ 加 kprintf 打 current/task 地址。**通用铁律**：改公共接口（返回值/字段语义）后，grep 所有用到该值的测试断言同步改；测试用 `Task tmp` + `set_current` 的范式，断言失败早返回会留悬垂 current——危险，清理应放断言之前或用 RAII。
20. **fork/clone 栈拷贝 full_used 须 < 栈大小，否则下溢踩邻接（F3-M2 批4 教训，2026-06-18）**：`full_used = parent->kernel_stack_top - current_rsp`，子栈 `stack_size`=16KB。若 `full_used > stack_size`（测试用 `kernel_stack_top=rsp+16384` 即触发），`child_stack_start = child_stack_virt + stack_size - full_used` 下溢到栈映射前 → memcpy 踩邻接内存（latent，堆布局变即命中要害）。fork/clone 加 `if (full_stack_used > stack_size) full_stack_used = stack_size;` 上限防御（生产永不触发，栈远未满）。测试 `kernel_stack_top` 用小偏移（rsp+4096）让 full_used < 栈大小。
21. **单测设 current 用 Scheduler::set_current，勿直接 g_per_cpu.current（F3-M3 批3 教训，2026-06-19）**：`Scheduler::current()`（scheduler.cpp:234）返静态 `current_`，**不是** PerCpu 的 `g_per_cpu.current`。两者只由 `Scheduler::set_current(task)`（scheduler.cpp:238）同步设置（current_ + g_per_cpu.current 都设）。单测装栈 task 作 current **必须用 `Scheduler::set_current(&t)`**，cleanup 用 `Scheduler::set_current(nullptr)`——只设 `g_per_cpu.current` 会让 `current_` 仍 nullptr，任何经 `Scheduler::current()` 的 handler（sys_setpgid/setsid/未来 waitpid 阻塞等）拿到 nullptr → ESRCH，测试 fail（F3-M3 批3 首验 825/2 fail 定位此）。test_clone 的 futex 侥幸（futex 内部不经 Scheduler::current()），掩盖了这层。**通用铁律**：单测设 current 一律用 Scheduler::set_current（两者都设），勿直接写 g_per_cpu.current。
22. **TaskBuilder().build() 消耗全局 tid 计数器,跨测试文件污染（F3-M4 批4 教训，2026-06-19）**：`next_tid` 是全局单例,`TaskBuilder().build()` 每次分配递增。测试文件**共享**一个计数器,执行序固定（`run_signal_tests()` 在 `run_scheduler_tests()` 前）。新测试用 TaskBuilder 建 victim 分到 tid 1/2/3 → 后跑的 `test_build_basic_task` 断言首任务 `tid==1`（test_scheduler.cpp:62）失败（实际 4+）。**根因非逻辑**,是共享全局态 + 脆弱断言。**修**：纯状态机测试（只碰 state/sched_class/sig_actions/sig_pending）用**栈 `Task t{}`**（同 test_sig_state 范式）,零 tid/slab/核栈消耗 → 不污染计数器。**通用铁律**：跨测试文件共享全局计数器（tid/pid/next_*）,新测试用 TaskBuilder 建任务会位移他测的「首任务 tid==N」断言;能不分配就不分配（栈 Task 优先）。

## ✅ direct-map 独立窗口（F2-M7 前置）Bug1 已修 — 2026-06-17（fresh build 734/0）

> 目标：修 `phys_to_virt` 的 latent >1GB direct-map bug（KERNEL_VMA 窗口只 1GB），为 buddy 接 PMM 铺路（buddy 侵入式链表写 high phys 需 identity direct-map）。
> 决策：独立窗口 `DIRECT_MAP_BASE=0xFFFF8800…`（PML4[272]，512GB）+ loader `direct_map_up_to`（1GB/2MB 大页 identity，不依赖 PDPE1GB）+ centralize `phys_to_virt`（phys_virt.hpp）+ 迁移 direct-map 站点（保留 kernel-image 站点 KERNEL_VMA）。详见 `document/notes/2026-06-17-direct-map-window.md`。
> **Bug1（批2 fresh build 暴露的 reserved-bits PF）已修（批3）**：根因 = `DmaPool::alloc` 对 direct-map 区域调 `g_vmm.map(virt=phys+DIRECT_MAP_BASE,…)` → `walk_level` 命中 direct-map 的 **1GB huge entry（PDPT[0]=0x83，PS bit）** → 触发 huge-split → 把 `pdpt[0]` 改成 4KB PT pointer → **破坏全局 direct-map** → 后续 `phys_to_virt` walk 错乱命中 phys 0 BIOS 数据当 PT → reserved PF（err=0x9，`test_cache_reads_real_file` 首次 AHCI init 触发）。诊断证实建后/load_elf 后页表正确（`PML4[272]=0x10003 PDPT[0]=0x83`），破坏在运行时。M3 时 DmaPool virt 在 KERNEL_VMA 窗口（2MB/4KB）map 无害，批2 迁 direct-map 后旧 map 变破坏源（迁移漏删）。修：删 `DmaPool::alloc` 的 `VMM.map`（direct-map 已被 loader 永久 identity 映射，不需 map，与 `return_pages` 只 free phys 不 unmap 对称，GOTCHA#7/#13）；`test_dma_pool` Test1 改 CPU round-trip（原 translate 断言依赖 split 副作用）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `DIRECT_MAP_BASE` 常量 + mini `direct_map_up_to`（2MB/1GB 大页 identity，PT@0x10000）+ loader 调用 + main_test identity 探针 | ✅ | f0b84ed | 734/0 + 探针 OK |
| 批2 | centralize `phys_to_virt`→DIRECT_MAP_BASE（删 4 重复）+ 迁移 direct-map 站点（page_cache/dma_pool/execve/usermode/process_new）+ 保留 kernel-image KERNEL_VMA + 测试跟进 | ✅ | 93ac379 | 增量 734/0 + host 49/0 + 实机不炸（**注**：fresh build 暴露 Bug1，此为增量态）|
| 批3 | **Bug1 修复**：删 `DmaPool::alloc` 的 `VMM.map`（walk_level split 1GB huge 破坏 direct-map）+ `test_dma_pool` Test1 translate→CPU round-trip + return_pages 注释订正 | ✅ | (本次) | **fresh build 734/0** |

**完成总结**：direct-map 独立窗口落地——loader 用 1GB（PDPE1GB）/2MB 大页把全 RAM identity 映到 `DIRECT_MAP_BASE`（PML4[272]，PT@0x10000），`phys_to_virt` 切到新窗口，全栈 direct-map 站点迁移。修 latent >1GB bug（页表/page_cache/DMA/execve/GS 对 high phys 现正确 identity）。关键教训：`-cpu max` 仅 KVM 时设→qemu64 无 PDPE1GB，须 2MB fallback；迁移严判 direct-map vs kernel-image；**direct-map 区域勿 `VMM.map/unmap/translate`**（walk_level 遇 1GB huge 会 split 破坏全局 direct-map，DmaPool 迁移漏删 map 教训，见 GOTCHA#13）。buddy 接 PMM（F2-M7 兑现）见下「F2-M7 Buddy PMM」段（批4a/4b，2026-06-18 完成，fresh KVM 742/0 + GUI 冒烟）。direct-map 前置就绪。

## ✅ F2-M7（Buddy PMM）已完成 — 2026-06-18（fresh KVM 742/0 + 实机 GUI 冒烟）

> 目标：buddy 伙伴系统替换 PMM flat bitmap（power-of-two order free lists），兑现 M7。direct-map 前置（上段）就绪。
> 决策：
> - **批4a**：cherry-pick buddy 批1 + `page_to_block`/`pop_free` KERNEL_VMA→DIRECT_MAP_BASE + low-first（fresh 742/0）。
> - **批4b**：PMM bitmap→buddy wiring（init/alloc/free/count + order_/bitmap_storage + `_locked` 锁契约）。
> - **Bug2 修正（批4b 核心）**：初版侵入式 free-list（next 指针写 free 页头经 direct-map）在 **WSL2 nested KVM（AMD）EPT 写读不一致** → free-list 链路振荡（valid↔poison `0xCAFEBABEDEADC0DE`）→ pop_free 遍历 #GP（`test_wm_close_button_closes_terminal_pipes`）。**TCG 2MB path 742/0 全绿**证明 buddy wiring 本身正确，根因是 nested KVM 物理层。**修 = buddy 改非侵入式 per-order bitmap free-list**（bitmap 存 metadata 区，不写 free 页，KVM nested safe；`find_first_set` 天然 low-first）。GOTCHA#14。
> 不做：SLAB（M7b 后续）/ CoW 共享内存（F3）/ 脏页写回。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批4a | cherry-pick buddy 批1 + `page_to_block`/`pop_free` 迁 DIRECT_MAP_BASE + low-first + test_buddy 适配 | ✅ | 2ad5442 | fresh 742/0（734+8 buddy 单测）|
| 批4b | PMM bitmap→buddy wiring（init/alloc/free/count + order_/bitmap_storage + `_locked`）+ **Bug2 修**：侵入式→bitmap free-list（nested KVM safe）+ test_buddy 适配 + qemu.cmake `CINUX_NO_KVM` env（TCG 诊断） | ✅ | (本次) | **fresh KVM 742/0 + 实机 GUI 启动到桌面** |

**完成总结**（F2-M7）：buddy 伙伴系统替换 PMM flat bitmap——`BuddyAllocator`（**per-order bitmap free-list**，1 bit/block，`find_first_set` 天然 low-first，非侵入式不写 free 页）+ PMM 接入（`init` 用 `buddy.init`+`mark_free_region` 排除 kernel/metadata 区；`alloc_page[_locked]`/`alloc_pages`/`free[_pages]`/count 调 buddy；`_locked` 保 IF=0 锁契约）+ order_ 数组（1B/页，head order 权威）+ bitmap_storage（per-order bitmap ~575KB @ order_storage 后）。关键教训 **GOTCHA#14**（nested KVM EPT 对 intrusive free-list 写读不一致 → 改 bitmap metadata 解；TCG 验证 buddy 逻辑正确）。验证：fresh KVM run-kernel-test 742/0 + 实机 `make run` GUI 启动到桌面不崩。遗留：SLAB（M7b）/ CoW（F3）。

## ✅ F2-M7b（SLAB 分配器）已完成 — 2026-06-18（fresh KVM 752/0 + host 48/0 + GUI 冒烟）

> 目标：buddy 之上分层小对象分配器 `SlabAllocator`，**全替 Heap**（不留 fallback「尾巴」），闭环 PMM(buddy)→Slab→kmalloc/kfree→operator new。小对象(≤2KB)走 Slab（9 通用缓存 + 专用缓存）；大对象(>2KB / 大对齐)走 **buddy + direct-map 复用**（DmaPool 同款 GOTCHA#7/#13，零 map/零元数据）。
> 决策（propose 已确认，2026-06-18）：
> - **#1 全替 Heap**：`kmalloc` 通用（小→Slab / 大→buddy+direct-map），Heap 删，无 fallback。
> - **#2 大对象复用 direct-map**：`virt=phys+DIRECT_MAP_BASE`，不 map/unmap；`kfree` 用 `phys=virt-DIRECT_MAP_BASE`+`free_pages`（buddy 记 order 权威，count 忽略）。免 KMEM_LARGE 区。
> - **#3 Slab 页 4K 映射**：侵入式 freelist 写在 4K 页内（KMEM_SLAB 区，PML4[256]），**绝不 direct-map huge**（GOTCHA#14/#15）。empty slab 可回收。
> - **#4 专用缓存现在做**：Task/Inode/VMA/CachedPage（现存类型）；Dentry 弃（不存在）；Pipe 走通用缓存。
> - **#5 Slab 返 nullptr on OOM**（operator new 链，freestanding 无异常，同 Heap 契约，非 ErrorOr）。
> - **#6 IF=0 安全**：PF handler(IF=0) 的 `new CachedPage`→kmalloc→Slab，须 IF=0 安全（批1 核 Heap::alloc 现状定 Slab 锁契约）。
> 删除面（批2）：`heap.{hpp,cpp}` + `test_heap.cpp` + CMake 行；改 `crt_stub.cpp`(7 重载)/`main.cpp:138`/`test/main_test.cpp:151`/`ram_block_device.hpp`(直接 g_heap.alloc/free 大块存储)。
> 不做：Heap 保底（全删）、Dentry 缓存、SLAB 着色/NUMA、性能 benchmark 套件（批4 仅方向断言）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `slab.hpp/cpp`（SlabAllocator 8 通用缓存 16-2048B + 页内 header + 侵入式 freelist + 4K 页 on-demand grow + KMEM_SLAB 区 + irq_guard IF=0）+ memory_layout KMEM_SLAB + CMake + `test_slab.cpp` 单测 | ✅ | 563bb0f | fresh KVM 750/0（742+8）+ host 49/0 |
| 批2 | `kmalloc/kfree`（小→Slab / 大→buddy+direct-map，free 按区路由）+ `crt_stub` 7 重载 + main/main_test init + `ram_block_device` 迁移 + **删 heap.{hpp,cpp}/test_heap(内核+host)** + slab 硬化（grow 页零化 + O(1) double-free 毒检）+ `test_kmalloc.cpp` 11 测 + `test_slab` +double-free 测 + **page_cache 按 `ino` 键控**（修 slab 复用 Inode 地址暴露的陈旧命中，GOTCHA#15） | ✅ | 4e05892 | **fresh KVM 751/0 + host 48/0 + 实机 GUI 到桌面** |
| 批3 | 专用缓存 API（`create_cache`/`cache_alloc`/`cache_free`，`kObjAlign`=16 支持非幂 obj_size）+ `dedicated_caches.cpp`（task/vma/cached_page，类专属 operator new/delete 自动路由，无调用点改动）+ `test_slab` +dedicated 测。**Inode 不入 slab**：ext2 用固定 `inode_cache_[]` 数组自管（非堆分配，N/A）。实测 Task=1008B→4/slab(原 1024 档 3)、VMA=56B→72/slab(原 64 档 63) 真省碎片；CachedPage=64B(2幂,接线为完整) | ✅ | 5d932e8 | **fresh KVM 752/0 + host 48/0 + 实机 GUI 到桌面** |
| 批4 | 收尾：ROADMAP/PLAN/todo/notes + GOTCHA + fresh KVM run-kernel-test + `test_host` + `make run` 冒烟 | ✅ | (本次) | fresh KVM 752/0 + host 48/0 + GUI 到桌面 |

**完成总结**（F2-M7b，4 批）：SLAB 分配器全替 Heap——批1 `SlabAllocator`（8 通用缓存 16-2048B + 页内 header + 侵入式 freelist + 4K 页 on-demand grow，KMEM_SLAB 独立 4K 区 PML4[256]，irq_guard IF=0 安全）；批2 `kmalloc/kfree`（小→Slab / 大→buddy+direct-map 复用，零 map/零元数据）+ crt_stub 7 重载 + ram_block_device 迁移 + **删 heap.{hpp,cpp}/test_heap（内核+host，净 -1951 行）** + slab 硬化（grow 页零化 + O(1) double-free 毒检）+ **修 page_cache 按 `ino` 键控**（slab 复用 Inode 地址暴露的陈旧命中 → sys_read 返错字节，GOTCHA#15）；批3 专用缓存 API（`create_cache`/`cache_alloc`/`cache_free`，`kObjAlign`=16 支持非幂 obj_size）+ dedicated_caches（task/vma/cached_page，**类专属 operator new/delete 自动路由**，无调用点改动）——实测 Task=1008B→4/slab(原 3)、VMA=56B→72/slab(原 63) 真省碎片；Inode 不入 slab（ext2 固定 `inode_cache_[]` 自管，N/A）。

架构：A.6 边界（Slab/kmalloc 返 nullptr on OOM，非 ErrorOr）；A.7 不入 Cinux-Base（依赖 PMM/VMM/heap）；侵入式 freelist 写 4K 页（绝不 direct-map huge，GOTCHA#13/#14）；大对象复用 direct-map（DmaPool 同款 GOTCHA#7/#13）；类专属 operator new/delete 让专用缓存对调用点透明（错误路径自动覆盖）。

关键教训 **GOTCHA#15**（slab 复用暴露按指针键控的缓存——page cache 按 `Inode*` 键控 → 新文件命中陈旧页 → `sys_read` 返错字节；Heap first-fit 侥幸不复用同地址故潜伏。改按 `inode->ino` 稳定号键控。**通用铁律**：按对象指针/地址键控的在线结构（cache/table），对象经分配器回收时必然陈旧命中，键须用稳定 id/number 非 pointer）。

验证：fresh KVM run-kernel-test 742→**752/0**（+10 slab/kmalloc/dedicated 单测 - 旧 heap 测 + page_cache 修复回归）+ host test_host **48/0** + 实机 `make run` GUI 启动到桌面不崩。**F2 内存管理增强里程碑收官（M1-M7 + M7b）**。

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

## ✅ F3-M1（信号系统）已完成 — 2026-06-18（fresh 783/0 + 实机 GUI 冒烟）

> 目标：从零构建 POSIX 信号（核心 22 个）：投递/处理/屏蔽 + Custom handler round-trip + kill/sigaction/sigprocmask/sigreturn + PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE 集成。解锁 TTY/shell（依赖瓶颈）。
> 决策（propose 已确认）：
> - **#1 Custom 走中断路径，不改 syscall.S**：syscall.S 精简帧（无 R12-R15）不能 sigreturn 完整恢复；Custom 在中断/异常返回路径投递（ISR 宏 `signal_check_deliver_isr`），sigreturn 经 IDT vector 0x80 trap gate（DPL=3）收完整 InterruptFrame。syscall 路径只 Default/Ignore。
> - **#2 栈注入 trampoline**：handler 返回地址指向栈上 `int $0x80`（cd 80）。依赖 NXE 关闭（GOTCHA#10），F9 启用迁 vdso。
> - **#3 MVP 范围**：核心 22 信号 + Default/Ignore/Custom；实时信号/sigaltstack/SA_RESTART/嵌套/STOP-CONT 真效果留后续。
> - **#4 SIGCHLD + waitpid non-blocking**：exit 投 SIGCHLD（default Ignore），waitpid 轮询，阻塞唤醒留 TODO（wait_queue 同 F3-M2）。
> 不做：实时信号、job-control 真调度、waitpid 阻塞、进程组 kill（F3-M3）、F9 NXE 后 trampoline 迁 vdso。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | signal.hpp（Signal enum 22 + SigSet ops + SigAction + default/uncatchable 查表）+ Task 信号字段 + fork 继承（清 pending）+ 单测 | ✅ | 860cf86 | 770/0（+7）|
| 批2 | signal.cpp（send/pick/exec_default/check_and_deliver + pid→Task 注册表）+ kill/sigaction/sigprocmask + syscall_dispatch 挂载 + 单测 | ✅ | bd558c8 | 780/0（+10）|
| 批3 | SignalFrame + signal_setup_frame + sigreturn_handler + 中断路径投递（ISR 宏 signal_check_deliver_isr）+ IDT vector 0x80 gate + int $0x80 trampoline + 单测 | ✅ | f9f0e9a | 782/0（+2）|
| 批4 | 集成：PF→SIGSEGV（handle_pf）+ exit→SIGCHLD（sys_exit）+ write→SIGPIPE + registry 单测 | ✅ | c623fbb | 783/0（+1）|
| 批5 | 收尾：libc signal wrapper（syscall.h/.cpp）+ 文档（PLAN/ROADMAP/todo/notes/GOTCHA）+ 实机冒烟 | ✅ | (本次) | 783/0 + GUI 启动到桌面 |

**完成总结**（763→783，F3-M1 +20）：POSIX 信号落地——`Signal`（22 核心号）+ `SigSet`（64-bit bitmask）+ `SigAction`（Default/Ignore/Custom）+ Task 信号字段（sig_actions[23]/sig_pending/sig_blocked，fork 继承清 pending）+ 投递（`signal_send` 设 pending / `signal_pick_deliverable` 选信号 / `signal_exec_default` 默认动作 / `signal_check_and_deliver` syscall 路径 / `signal_check_deliver_isr` 中断路径）+ pid→Task 注册表（sys_kill 查找）+ signal frame（用户栈构造 + `int $0x80` trampoline）+ sigreturn（vector 0x80 gate 收 InterruptFrame 恢复）+ 集成（PF→SIGSEGV / exit→SIGCHLD / write→SIGPIPE）+ libc wrapper（sys_kill/sys_sigaction/sys_sigprocmask）。架构：A.6 边界（signal_send 返 -errno）；syscall handler 6 参；Custom 投递挂中断返回路径（避开 syscall.S 精简帧 sigreturn 限制）；栈注入 trampoline（NXE 关闭可行，F9 迁 vdso）。

关键教训 **GOTCHA#16**（sigreturn 栈注入依赖 NXE 关闭 + Custom 走中断路径 + signal_check_deliver_isr 严判 cs&3）。

验证：fresh run-kernel-test 763→**783/0**（+20 signal 单测）+ 实机 `make run` 生产内核启动到 GUI 桌面（Desktop icons / gui_worker），signal_check_deliver_isr 在每个中断（PIT/AHCI）后高频跑全程不炸，kernel_init exit→SIGCHLD 投递不炸。**Custom handler 真用户态 round-trip 留后续**（libc wrapper 已就绪，需用户程序触发）。下个焦点：F3-M2 clone + futex + TLS。

## ✅ F3-M2（线程支持：clone + futex + TLS）已完成 — 2026-06-18（5 批，783→810）

> 目标：为 musl/pthread 打内核地基——`clone`(56) Linux 风格线程原语 + `futex`(202) 用户态互斥 + TLS（FS 段基址 MSR_FS_BASE）+ 线程组（tgid / CLONE_CHILD_CLEARTID exit futex_wake）。
> 决策（propose 已确认）：
> - **#1 共享资源 refcount 指针化（非 MVP 拷贝）**：sig_actions / fd_table / cwd 都重构为引用计数共享对象。CLONE_SIGHAND/FILES/FS 真共享（POSIX 正确，musl pthread 带这些 flag）；fork 仍 copy 语义（fork 不带 CLONE_*，子建新对象）。代价：批3 重构侵入 signal.cpp/fork/所有 sig_actions+cwd 访问点，但批4 clone 才能正确共享。
> - **#2 futex = WAIT/WAKE + BITSET**（多 uint32 掩码匹配，pthread 条件变量用）；**不做** timeout（需 PIT timer 定时唤醒）/ PI / requeue。
> - **#3 clone 子进程用户栈返回**：复用 fork「拷父核栈 + relativize + trampoline」机制，patch 拷贝帧的 user_rsp 槽 = `stack` 参数（syscall.S 帧 `rsp+0=user_rsp`，via fork delta 相对化定位）。备选 Approach C（clone_child_trampoline 直跳用户态复用 jump_to_usermode）若 patch 太脆。
> 不做：futex timeout/PI/requeue、实时信号线程语义、job-control 真调度、进程组 kill（F3-M3）。
> 依赖就绪：M1 信号（mask 继承）、F2 mmap（线程栈）、scheduler block/unblock、Spinlock、ErrorOr、next_tid、per_cpu。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | **TLS（fs_base）**：`CpuContext` 加 `fs_base`(offset 80, alignas(16) 填充 sizeof 80→96)+static_assert；context_switch.S 存/恢复 fs_base(MSR_FS_BASE=0xC0000100) 与 gs_base 同段；`kernel/arch/x86_64/tls.{hpp,cpp}` `cinux::arch::set/get_tls_base`(wrmsr/rdmsr)；task_builder 新核线程显式 fs_base=0；单测 round-trip（值须**规范地址**否则 wrmsr #GP）。**GOTCHA：FS_BASE 须规范地址** | ✅ | 8f2805b | 785/0（+2） |
| 批2 | **futex**：`sys_futex.{hpp,cpp}` 256-bucket hash + 侵入式 Waiter(`Task::wait_next`)+Spinlock（镜像 Mutex/Semaphore 的 block/unblock 范式）；FUTEX_WAIT(直接 deref `*uaddr`，≠val 返 EAGAIN，入队→Scheduler::block，醒后出队返 0)/WAKE(出队最多 val→unblock，返唤醒数)/BITSET(uint32 掩码匹配)；Task 加 futex_uaddr/futex_bitset；注册 SYS_futex=202；单测。**陷阱：测试用 g_per_cpu.current（不 set_current）避 block 挂死；全局表跨测试残留须每测 wake 清理** | ✅ | 2f1c331 | 794/0（+9） |
| 批3 | **共享资源 refcount 基建 + retrofit fork**：`SharedSigActions`(refcounted 堆对象持 SigAction[23]，Task 指针化，signal.cpp/fork/所有 `sig_actions[n]` 访问改 `->actions[n]`) + `FDTable` 加 refcount(acquire/release) + `SharedCwd`(refcounted 指针化，getcwd/chdir/execve/fork 全访问点改)；fork copy 语义建新对象（memcpy 后立即 create_copy，error-path delete 不碰父）；operator delete 经 release_resources 释放三者；acquire/release + 单测。**陷阱：fork memcpy 拷指针须立即 detach+copy；Task slab 无 ctor 须手工分配；测试 Task t{} 须手动 create** | ✅ | 5a8d251 | 801/0（+7） |
| 批4 | **线程组 + clone 核心**：Task 加 `tgid`/`group_leader`/`clear_child_tid`/`set_child_tid`；`sys_getpid` 返 tgid；`sys_clone.{hpp,cpp}`+clone()（复用 fork 拷核栈机制）；CLONE_VM/FILES/SIGHAND/FS(共享 批3 对象)/THREAD(tgid=父 tgid,兄弟)/SETTLS(设 fs_base)/SETTID/CLEARTID(记地址)；**子进程用户栈返回**（patch 帧 user_rsp 槽 `kernel_stack_top-96`=stack，GOTCHA#18）；fork/clone 加 full_used 上限防御；注册 SYS_clone=56；test_clone 8 测 + 修 test_fork_exec（rsp+4096+remove_task）+ getpid 测试设 tgid。**陷阱：getpid→tgid 使旧 getpid 测试断言失败→TEST_ASSERT 早 return→跳过 set_current→current_ 悬垂崩（非踩踏）** | ✅ | 8808c2c | 809/0（+8） |
| 批5 | **集成 cleartid + libc + 收尾**：`task_exit_cleartid`（线程退出写 0 到 clear_child_tid + `futex_wake_addr` 唤醒 joiner，sys_exit 调用）；futex_wake_addr 暴露内核内部 wake；libc `sys_clone`/`sys_futex` wrapper + `_syscall5`/`_syscall6`(r10/r8/r9) + CLONE_*/FUTEX_* 常量；cleartid 单测。**真用户态线程 round-trip + 实机 GUI 冒烟留 follow-up**（需用户线程程序） | ✅ | fe7b535 | 810/0（+1） |

**架构契合**：A 翻译边界（clone/futex 返 -errno：EAGAIN/EINVAL/ESRCH/ENOMEM，内核内 ErrorOr，仅 trap 入口翻 errno）；A 禁 RTTI（按字段共享，无 dynamic_cast）；A.7 不入 Cinux-Base（依赖 PMM/VMM/scheduler/heap）；层化 arch(tls)/proc/scheduler/syscall 各司其职不反向依赖；对齐 Linux（clone flags/FUTEX op/线程组语义，CONFIG 风格不重造轮子）。

**风险**：
- **R1（最高）clone 子进程用户栈返回**：syscall.S 帧 `rsp+0=user_rsp / rsp+8=user_rip`，子进程拷来的帧须把 user_rsp 槽 patch 成 `stack` 参数（用 fork 现成 delta 相对化 `(child_addr = parent_slot - current_rsp + child_stack_start)`）。批4 实测验证；备选 Approach C（clone_child_trampoline 直跳用户态，复用 jump_to_usermode）。
- **R2 共享 refcount 生命周期**：线程 exit 不释放共享表（fd/sig/cwd），仅 group-leader 进程 exit 最后 release 到 0 才释放。retrofit fork 须保 fork copy 语义不破现有测试。
- **R3 启动路径**：Task 结构变（新字段 + sig_actions/cwd 指针化）+ context_switch.S 变 → run-kernel-test 全 kernel-mode 不覆盖 ring3 线程路径，实机冒烟必做（GOTCHA#11 同类）。
- **R4 futex 无 timeout**：FUTEX_WAIT 无限等，waker 必须存在；测试用 2 task（一 wait 一 wake）配对。

**GOTCHA（新增预留）**：#18 clone 子进程用户栈返回（帧 user_rsp 槽 patch）+ 共享 refcount 生命周期（线程 exit 不释放共享表，进程 exit 最后释放）。批1 已落 GOTCHA#17（wrmsr FS_BASE 须规范地址）。

## ✅ F3-M3（进程组/会话 + waitpid 阻塞）完成 — 2026-06-19（5 批，810→827）

> 目标：为 Job Control / TTY 打地基 —— 进程组（pgid）+ 会话（sid）+ `setpgid`/`getpgid`/`getsid`/`setsid`/`killpg` + fork 继承，**顺带补 waitpid 阻塞**（闭环 F3 进程管理，shell/CFBox 不再忙等烧 CPU）。
> 决策（propose 已确认）：
> - **#1 范围 = 进程组 + waitpid 阻塞**（非纯 todo 原样，也非全包 STOP/CONT 真调度）。STOP/CONT 真调度留 M4 调度器。
> - **#2 继承规则方案 A + 可测 helper**：`inherit_process_identity`（process_internal.hpp），root fork（`parent->pgid==0`）自成组，否则继承父。fork/clone memcpy 后显式调用。init(pid=1) 自动满足（kernel_init pid=0 fork 出 pid=1 自成组）。
> - **#3 killpg 放 signal.cpp**（持 `g_registry_head` + `signal_send`，遍历按 pgid 广播）；process_group.cpp 只做纯字段 setpgid/getsid/setsid。
> - **#4 waitpid 阻塞复用 `Scheduler::block/unblock` + exit 唤醒 parent**，不引入新 wait_queue。
> - **#5 controlling_tty 只占位**（-1），真控制终端 attach 留 F10-M3 TTY。
> 不做：SIGSTOP/CONT 真调度（M4）、实时信号组语义、权限检查（全 root）。
> 复用现成基建：pid registry（`signal_find_task_by_pid` + `g_registry_head`）、Task children/parent、`Scheduler::block/unblock`（futex 同款）、`signal_send`。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | Task 加 `pgid`/`sid`/`session_leader`/`controlling_tty` + `inherit_process_identity`（root 自成组 / 否则继承父）+ fork/clone 调用 + 5 单测 | ✅ | 77be415 | 815/0（+5） |
| 批2 | `process_group.{hpp,cpp}`：setpgid/getpgid/getsid/setsid（纯字段语义）+ killpg（signal.cpp 遍历 registry 按 pgid 广播）+ sys_kill pid<0 接 killpg 闭环 TODO + 9 单测 | ✅ | 824449c | 824/0（+9） |
| 批3 | 4 syscall（setpgid=109/setsid=112/getpgid=121/getsid=124）+ 注册 + libc wrapper + 3 端到端单测 | ✅ | b228f67 | 827/0（+3） |
| 批4a | **exit Dead→Zombie 契约修正**：sys_exit Zombie + dequeue（对齐 exit_current）+ schedule(275) 跳 Zombie（pick_next 不查 state，Zombie 留 queue 会崩） | ✅ | ee13cac | 827/0 回归 + 实机 GUI 到桌面 |
| 批4b | **waitpid 阻塞**：默认 block、`WNOHANG` 非阻塞；exit 唤醒 waiting parent；terminal/test 全改 WNOHANG 防挂死 | ✅ | 734d6a1 | 827/0 + 实机 GUI 到桌面 |
| 批5 | 收尾：文档（PLAN/ROADMAP/todo/notes/GOTCHA）+ 全量验证 + 实机冒烟 | ✅ | (本次) | 827/0 + host + 实机 GUI 到桌面 |

**风险（propose 预判）**：
- **R1（最高）批4 破现有 waitpid 测试**：默认行为从 non-blocking 变 blocking，现有 `test_fork_exec` 等若没传 `WNOHANG` 且无 zombie → 挂死。批4 第一步 grep 全部 waitpid 调用点审计（GOTCHA#19 同款家族 + futex 单测 block 挂死坑）。
- **R2 exit 唤醒**：`sys_exit` 在 scheduler 路径，`unblock(parent)` 要在 block/unblock 锁契约内（IF=0 / irq_guard），参考 futex wake。
- **R3 killpg 迭代安全**：遍历 registry 广播时 task 可能正退出 → 持锁或先收集 pid 再发。

## ✅ F3-M4（调度器接口验证与增强）完成 — 2026-06-19（5 批，827→840）

> 目标：验证 SchedulingClass 插拔接口完备性 + 小幅增强(优先级/多类),并兑现 M3 留的
> "SIGSTOP/CONT 真调度效果(TASK_STOPPED 状态机)"。**不引入新调度器实现**(todo 铁律),
> 向后兼容(生产单类场景行为不变)。
> 决策（propose 已确认）：
> - **#1 T1 接口钩子给默认实现**(非纯虚):task_tick/task_fork/task_deadline 基类默认
>   no-op/false/0,现有子类零改动;时间片量子从 `Scheduler::current_slice_` 移入 RoundRobin
>   (`quantum_remaining_`),`tick()` 委托给类——抢占策略内聚。
> - **#2 T2 优先级小值优先**:pick_next 扫描选 `priority` 最小者,并列取最早入队(FIFO),
>   故同优先级 RR;严格优先级(可饿死),对齐 todo「简单实现」。
> - **#3 T3 pick_next_from(classes,count) 公开原语**:数组入参,脱离全局 `default_rr_` 残留态
>   单测(传本地类数组);schedule/exit_current/run_first 三处 `default_rr_.pick_next()` 改走
>   `pick_next_task()`(绑全局 classes_),让多类机制真正生效。注册序即优先级(不加 class-priority 参数)。
> - **#4 STOP/CONT 发送时恢复(关键)**:Stopped 任务永不被调度无法自投递 SIGCONT/SIGKILL,
>   故 `signal_send` 见 Stopped+(SIGCONT|SIGKILL) 立即 Ready+enqueue;SIGCONT 另清 pending stop。
>   schedule() 守卫加排除 Stopped。
> 不做:CFS/防饿死、task_fork 接 fork 路径(memcpy 已拷 priority,等价 noop)、
> SIGKILL/SIGTERM 唤醒 Blocked(可中断睡眠,更大改动)、waitpid 报告 stopped(WUNTRACED)。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | **T1 接口钩子**:SchedulingClass 加 task_tick/task_fork/task_deadline(基类默认实现)+ RoundRobin 接管量子(task_tick 2-tick 抢占 + 重充,pick_next 重置,task_fork 继承 priority)+ tick() 委托 + 删 current_slice_ | ✅ | 8b6c46e | 830/0(+3) |
| 批2 | **T2 优先级 RR**:pick_next 扫描选 priority 最小者(并列 FIFO→同级 RR)+ 抽 remove_at_locked 助手共享环形紧凑(dequeue/pick_next 复用) | ✅ | f3b0493 | 833/0(+3) |
| 批3 | **T3 多类查询**:pick_next_from(classes,count) 公开原语 + pick_next_task() 私有包装;schedule/exit_current/run_first 改走它(不再绕过 classes_[]) | ✅ | d57bb41 | 836/0(+3) |
| 批4 | **STOP/CONT 真调度(M3 follow-up)**:TaskState::Stopped + signal_exec_default kStop(Stopped+dequeue+maybe schedule)/kContinue(恢复) + signal_send 发送时恢复(SIGCONT/SIGKILL)+清 pending stop + schedule 守卫排除 Stopped | ✅ | e9b0dd4 | 840/0(+4) |
| 批5 | 收尾:T4 头部伪代码(pluggable scheduling 示例)+ ROADMAP/PLAN/todo/notes + fresh run-kernel-test + `make run` 实机冒烟 | ✅ | (本次) | 840/0 + 实机 GUI 到桌面 |

**架构契合**：A 翻译边界(signal_send 返 -errno,内核内直接改状态);A 禁 RTTI(SchedulingClass virtual 多态);A.7 不入 Cinux-Base(依赖 Task/Scheduler);对齐 Linux(priority 小值优先、SIGCONT 发送时恢复 + 清 pending stop、CONFIG 风格不重造轮子)。

**完成总结**（827→840，F3-M4 +13）：调度器插拔接口完备化 + STOP/CONT 状态机落地——①SchedulingClass 三策略钩子(默认实现,时间片内聚到类,删 current_slice_ 单一事实源在 RoundRobin::quantum_remaining_);②优先级感知 RoundRobin(pick_next 选最小 priority,同优先级 FIFO 轮转,remove_at_locked 助手去重);③多调度类实际查询(pick_next_from 数组原语让遍历可单测,schedule/exit/run_first 不再绕过 classes_[],生产单类场景等价=向后兼容);④SIGSTOP/SIGCONT 真调度(TaskState::Stopped 状态机,signal_send 发送时恢复 Stopped 目标 + 清 pending stop,schedule 守卫排除 Stopped)。T4 头部伪代码示例(如何加新调度算法:继承 SchedulingClass + register_class)。

关键教训 **GOTCHA#22**(TaskBuilder 消耗全局 tid 计数器跨测污染——批4 首版用 TaskBuilder 建 victim 分到 tid 1/2/3,致 test_build_basic_task 的 tid==1 断言失败;改栈 Task t{} 零消耗解。**通用铁律**:跨测试文件共享全局计数器,新测试用 TaskBuilder 建任务会位移他测「首任务 tid==N」断言;纯状态机测试用栈 Task)。

验证：fresh run-kernel-test 827→**840/0**(+13 接口/优先级/多类/stop-cont 单测 - 0 删,827 回归全绿=向后兼容铁证)+ 实机 `make run` GUI 启动到桌面不崩(kernel_init/gui_worker 经新 pick_next 路径,无 panic/halt)。**诚实记录**:① schedule/exit/run_first 改写靠 827 回归覆盖(高危路径);② STOP/CONT「停止当前任务→schedule 切走」路径无法单测(context switch 同 block/futex 坑)+ 生产启动无 SIGSTOP 投递(实机也覆盖不到)→ 靠逻辑正确性(复用 block 范式)+ 留真 shell job-control 程序端到端验证。**F3(进程与线程)全里程碑收官(M1-M4 ✅)**。
