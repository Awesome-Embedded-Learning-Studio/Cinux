# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M2 = 内核日志（dmesg 级）**：Cinux-Base `Logger`/`LogLevel`/`RingBuffer` 已就绪（✅），本里程碑增量是内核持久化——`ConcurrentRingBuffer`（MPSC，M1 推迟项）+ `KernelLog`（历史 ring buffer sink）+ kprintf 桥接 + `sys_dmesg`。同 M0/M1 消费迁移思路。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

## 批表

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `ConcurrentRingBuffer<T,N>`（kernel/lib/，MPSC：RingBuffer+Spinlock，push/pop/batch 单次加锁）+ 测试 | 🔄 | — | — |
| 批2 | `KernelLog`（LogEntry+单例+ring sink 注册到 Logger+log/read/dropped）+ `klog_*` 宏 + kprintf 攒行桥接 + 测试 | ⏳ | — | — |
| 批3 | `sys_dmesg` syscall（read→`[LEVEL] tick: msg\n`→用户态）+ syscall 编号 + 测试 | ⏳ | — | — |
| 批4 | grep kprintf 迁移范围报告（仅规模/分布，不迁移） | ⏳ | — | — |
| 收尾 | 文档(本文+ROADMAP+todo+DEVLOG) + 全量 run-kernel-test | ⏳ | — | — |

## 范围裁定
- **Cinux-Base 已就绪 → 复用**：`Logger`（单例/多 sink/level/printf/dropped）、`LogLevel`、`RingBuffer`。**不在 kernel/ 重定义 LogLevel**（修正 todo T1 规划——那会违反子模块边界）。
- **内核增量**：`ConcurrentRingBuffer`（用 kernel Spinlock，故放 kernel/lib/ 而非 Cinux-Base——后者 freestanding 无锁概念）、`KernelLog`、kprintf 桥接、`sys_dmesg`、`klog_*` 宏。
- **推迟**：todo T6「全量迁移现有 kprintf → klog_*」是大工程（kprintf 遍布内核），批4 仅 grep 报范围，迁移留后续；`kprintf` 保留用于早期启动（ring buffer 未就绪时）。

## OPEN GOTCHAS
1. **验证 target**：内核改动用 run-kernel-test（~662 项）；host 单测不在其中，改被 mock 类后 push 前补全量编译（L5）。
2. **Cinux-Base 是子模块**：`Logger`/`LogLevel`/`RingBuffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`，复用勿重写。
3. **R1（最高）kprintf 攒行并发**：kprintf sink 逐字符、Logger sink 整消息。多 CPU 同时 kprintf 逐字符攒行会**交错**。批2 须锁内攒行（Spinlock 保护行缓冲）或 per-CPU 缓冲。
4. **R2 中断上下文**：日志可能在 IRQ 上下文（panic/驱动），`ConcurrentRingBuffer` 的锁须禁中断保护（irq_save），否则死锁。
5. **R3 启动顺序**：kprintf 早期就绪（serial），KernelLog ring buffer 后初始化；klog sink 注册时机避免早期日志丢失/空指针。
6. **R6 LogEntry 固定 message 长度**：长消息截断，与 Logger formatted message 协调。

## M2 基础设施笔记
- Cinux-Base `Logger`：`instance()` 单例、`register_sink(LogSink, ctx)`（`LogSink=void(*)(LogLevel,const char*,void*)`）、`log(level,fmt,...)`、`set_level`、`dropped_count`。**非线程安全**（caller 同步）。
- kprintf sink：`OutputSink=void(*)(char,void*)` 逐字符，`kprintf_register_sink`。
- 桥接方向：kprintf 逐字符 → 攒行（遇 `\n`）→ `Logger::instance().log(INFO, line)`；ring buffer 作为 Logger 的一个 sink 存历史供 dmesg 读。
- 时间戳：`kernel/drivers/pit/pit.hpp` tick 计数（单核；SMP 后需全局/per-CPU tick）。
- 跨里程碑通用 gotcha（M1 遗产）：grep 调用方两种形态（箭头+点号）；多 Edit 应用过程 IDE 诊断是中间态噪音、以编译为准；里程碑定义区分「类型就绪」vs「内核消费」。
