# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M3 = DMA 基础设施 🔄 进行中（2026-06-16 启动）**；M2 内核日志 ✅ 完成。
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

## 🔄 F1-M3（DMA 基础设施）— 进行中（2026-06-16 启动）

> 目标：设备无关 DMA 基建，收编 ad-hoc（PMM + VMM + 硬编码 phys→virt 偏移 `0xFFFFFFFF80000000ULL`）。下游契约 = F5-M1 AHCI（`DmaPool.alloc()→DmaBuffer` + `PrdtBuilder`）。
> 决策：PrdtBuilder 纳入 M3（批3）；归属 `kernel/drivers/dma/`；phys→virt 用 `VMM.map`（批2 定）。
> 不碰 `ahci.cpp`(F5-M1)/`IBlockDevice`(M4)；不动早期启动 ad-hoc（PMM 就绪前用不了，OPEN GOTCHA #5 同类启动序约束）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | DmaBuffer（move-only，phys/virt/size，RAII 析构归还）+ 测试 | ⏳ | — | — |
| 批2 | DmaPool（`ErrorOr<DmaBuffer>`，封装 PMM+VMM+对齐，消除 magic）+ 测试 | ⏳ | — | — |
| 批3 | PrdtBuilder（scatter-gather segment 列表，>4KB 拆段）+ 测试 | ⏳ | — | — |
| 批4 | 收尾：memory_layout.hpp 注释语义化 + notes + 全量验证 | ⏳ | — | — |

架构契合：A.6 `ErrorOr<DmaBuffer>`（内部 PMM/VMM bool→Error 转译）；A.7 不入 Cinux-Base（依赖 PMM/VMM 破无堆）；A.8/9 复用 cinux::lib。风险：phys→virt 策略(VMM.map)、页内对齐、启动序。

## OPEN GOTCHAS（跨里程碑通用，活警告）
1. **验证 target**：内核改动用 run-kernel-test（~674 项）；host 单测（`test/unit/`）不在其中，改被 mock 类后 push 前补全量编译（L5）。
2. **Cinux-Base 是子模块**：`Logger`/`LogLevel`/`RingBuffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`，复用勿重写。
3. **里程碑区分「类型就绪」vs「内核消费」**（M0/M1/M2 同构）：Cinux-Base 类型常先就绪，待办是 kernel/ 增量消费。
4. **klog_* 实时输出的 reentrancy guard**（批4a）：`g_klog_emit_depth` 让 klog sink 跳过 log 自身的 kprintf，避免同一行双重入队——是 kprintf→klog_* 迁移的前提（否则迁后丢 console）。定义须在 `log()` 前。
5. **崩溃 ring 读不到**：fatal_halt/kpanic 死循环，ring 历史崩溃后读不到；非崩溃异常(#DB/#BP/#GP-user)/error 进 ring 可 dmesg 读。kpanic/dump 保留 kprintf（实时诊断/halt）。
6. **kprintf 全量迁移未做**：294 个（除 mini 148）留渐进；mini 148 不迁（早期启动无 ring）。vkprintf_impl 第三参是 va_list 非 va_args，需 va_list 或手动 format。
