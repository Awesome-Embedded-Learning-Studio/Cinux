# 2026-06-16 F1-M2 内核日志 — dmesg 级日志系统

## 背景

CinuxOS 此前只有 `kprintf`（逐字符多 sink，实时 serial/framebuffer），**无结构化日志历史**——启动后无法回看"之前发生过什么"。dmesg 是内核可观测性的基础（调试、审计、用户态诊断）。

Cinux-Base 子模块已就绪：`Logger`（level 过滤 + 多 sink + printf 格式 + dropped 计数）、`LogLevel`、`RingBuffer`。本里程碑的增量是把这些"叶子能力"组合成内核的**持久化日志系统**——同 M0/M1 的消费迁移模式：类型就绪，工作是 kernel/ 增量消费。

## 目标

dmesg 全链路闭环：

```
kprintf / klog_*  →  KernelLog ring buffer（IRQ 安全，level + 时间戳）
                              ↓
                        sys_dmesg (103)  →  用户态读取 "[LEVEL] tick: msg"
```

## 设计

### 分层（复用 Cinux-Base，增量在 kernel/）

| 层 | 来源 | 内容 |
|----|------|------|
| `LogLevel` / `Logger` / `RingBuffer` | Cinux-Base（复用） | 类型就绪，不重写 |
| `ConcurrentRingBuffer<T,N>` | `kernel/lib/`（新） | RingBuffer + `Spinlock::irq_guard`，MPSC、IRQ 安全 |
| `KernelLog` | `kernel/lib/`（新） | `LogEntry` ring + `klog_*` 宏 + kprintf 攒行 sink |
| `sys_dmesg` | `kernel/syscall/`（新） | `SYS_dmesg=103`，格式化历史读取 |

`ConcurrentRingBuffer` 放 `kernel/lib/` 而非 Cinux-Base——因为它依赖 `kernel/proc/sync.hpp` 的 Spinlock，而 Cinux-Base 是 freestanding（无锁概念）。它是 M1 明确推迟过来的 MPSC 封装。

### ConcurrentRingBuffer — IRQ 安全 MPSC

每个操作（`push/pop/push_batch/pop_batch/clear/empty/full/size`）经 `Spinlock::irq_guard()`（禁中断 + 获取锁）。这让日志 sink 可在 **IRQ / panic 上下文**安全调用——`Spinlock::irq_guard()` 已存在于 [sync.hpp](../../kernel/proc/sync.hpp)，无需自己 `irq_save`。`capacity()` 是编译期常量，不需锁。

### KernelLog — 历史存储 + kprintf 桥接

- `LogEntry { uint64_t timestamp; LogLevel level; char message[256]; }`，存于 `ConcurrentRingBuffer<LogEntry, 128>`（~33 KiB 全局；todo 规划的 4096 会占 ~1 MiB，过大）。
- `log(level, fmt, ...)`：`vkprintf_impl` 格式化 → 实时 console 输出 + push ring。
- `klog_*` 宏（debug/info/warn/error）→ `KernelLog::instance().log`。
- **kprintf 攒行 sink**：`kprintf_register_sink(klog_kprintf_sink)` 让**未迁移的裸 kprintf** 也进 ring（逐字符攒行，遇 `\n` 刷一条 `INFO` LogEntry）。sink 经 `irq_guard` 保护，解多 CPU 并发交错（kprintf 遍历 sink 无锁）。

### sys_dmesg — 用户态读取

`SYS_dmesg(buf, len)`：drain KernelLog 条目，格式化 `[LEVEL] tick: message\n` 写入用户 buf。canonical 地址检查（同 sys_read，`-EFAULT`）。编号 103 对应 Linux `SYS_syslog`。

## 关键决策

- **KernelLog 独立，不复用 Cinux-Base Logger**：Logger 非线程安全且其 sink 是"整消息输出"概念，与"ring 历史存储"混杂。KernelLog 直接用 ConcurrentRingBuffer（已 IRQ 安全）更干净。
- **read 用 drain 语义**（消费）：简化；非消费快照读留后续（dmesg 需要重复读时再加）。
- **klog_* 必须实时输出**（批4a）：否则把 kprintf 迁成 klog_* 后丢失 console 实时调试。`KernelLog::log` 内部 `kprintf("[LVL] msg")` 实时 + push ring。
- **高价值迁移优先**（批4b）：kprintf 全量迁移是 ~294 个（除 mini 148）的大工程；只迁 exception_handlers 的 `[FATAL]`/`[EXCEPTION]`（13→klog_error、5→klog_warn）到统一 API，其余留渐进。

## 陷阱

### vkprintf_impl 第三参是 va_list，不是可变参数

`vkprintf_impl(out_fn, fmt, va_list args)`——kprintf.cpp 的用法佐证。sys_dmesg 的字段是固定值（level/tick/msg），不是 va_list，**不能直接用它 format 多个值**。改用手写 `append_char/append_str/append_u64` helpers 直写 buf（少一次 copy，更高效）。

### klog_* 实时输出的双重入队

`KernelLog::log` 实时 `kprintf` 会再经 klog sink 攒行 → 同一行 push 两次。用 reentrancy guard `g_klog_emit_depth`：log 输出前置 1，klog sink 检查 `>0` 跳过。**定义必须在 `log()` 之前**——首版放在文件后部 anonymous namespace，导致 `log()` 处 undeclared 编译红。

### 崩溃 ring 历史读不到

`fatal_halt`/`kpanic` 死循环，ring 历史崩溃后读不到（用户态 dmesg 不再跑）。所以 **kpanic + register dump 保留 kprintf**（实时诊断 + halt）；只有非崩溃异常（`#DB`/`#BP`/`#GP`-user，系统继续）和一般 error 进 ring 才对 dmesg 有意义。迁移崩溃路径的 [FATAL] 价值在 API 统一 + 实时（批4a 保证），不在 ring。

## 验证

run-kernel-test **662 → 674**（+12 新测试）：
- ConcurrentRingBuffer 5（FIFO / full-drop / batch+wrap / clear / capacity）
- KernelLog 4（FIFO / level 过滤 / dropped-on-full / kprintf sink 攒行）
- sys_dmesg 3（编号 / null-buf EFAULT / 格式化）

全为 QEMU in-kernel 测试，无 host 单测涉及。

## 遗留

- **kprintf 全量迁移**：294 个（除 mini 148）未迁，留渐进按子系统做。mini 148 不迁（早期启动加载器，ring buffer 未就绪）。
- **read drain → 快照**：dmesg 需重复读时改非消费快照。

---

commit：`974e406`（批1 ConcurrentRingBuffer）、`d2936a6`（批2 KernelLog）、`4b3b95f`（批3 sys_dmesg）、`cbcbb3a`（批4a 实时输出）、`809bf7d`（批4b exception 迁移）。
