# 2026-06-16 F1-M1 RingBuffer 消费迁移 — 统一 kernel/ 环形缓冲到 Cinux-Base

## 背景

CinuxOS 的 `kernel/` 里散落着多套**手写环形缓冲**，彼此策略还不一致：

| 位置 | 实现 | 满空判定 | 容量 |
|------|------|---------|------|
| `kernel/ipc/pipe.cpp` | `buffer_[4096]/head_/tail_/count_` + 两段回绕拷贝 | `count_` 区分（不牺牲槽位） | 4096 |
| `kernel/drivers/keyboard/keyboard.cpp` | `queue_[64]/head_/tail_` | 牺牲一个槽位（`head_==tail_` 空、`next==head_` 满） | 实际 63 |

与此同时，Cinux-Base 子模块早已提供 `cinux::lib::RingBuffer<T,N>`（freestanding、header-only、有完整测试与 API 文档，component-index 明写"用于日志和管道"）——而且它用的正是 `count_` 区分满空的策略，与 pipe 的手写实现**完全一致**。

## 目标

消灭 `kernel/` 里的重复实现，回归 DIRECTIVES 的层化铁律——「`kernel/` 消费 `cinux::lib`，不在 `kernel/` 重写」。同 M0（ErrorOr 迁移）的模式：**类型已就绪，工作是消费迁移，不是造轮子**。

## 实现

### pipe（批1）

[`kernel/ipc/pipe.{hpp,cpp}`](../../kernel/ipc/pipe.cpp)：手写 `buffer_/head_/tail_/count_` 及 `write/read/try_read/try_write` 的两段回绕拷贝，替换为 `cinux::lib::RingBuffer<char, 4096>` 的 `push_batch/pop_batch`。**外层语义原样保留**：`Spinlock` + `irq_save/restore`、阻塞 spin-wait（`PIPE_SPIN_WAIT_ITERS`）、reader/writer close 的 EOF 语义都不动——RingBuffer 只接管存储层。

净减约 109 行（两段回绕拷贝被 `push_batch/pop_batch` 取代）。

### keyboard（批2）

[`kernel/drivers/keyboard/keyboard.{hpp,cpp}`](../../kernel/drivers/keyboard/keyboard.cpp)：`queue_[64]/head_/tail_`（牺牲槽位）替换为 `RingBuffer<KeyEvent, 64>` 的 `push/pop`。`enqueue` 满 drop-newest 由 `push` 返 false 实现；`InterruptGuard` 同步保留。公共接口（`init/irq1_handler/poll`）签名不变 → `sys_read`/GUI/main 等调用方零波及。

## 关键决策

- **容量 63 → 64**：原 keyboard 牺牲一个槽位（实际容量 63），RingBuffer 用 `count_` 不牺牲（容量 64），更贴合 `KEY_QUEUE_SIZE` 字面值。测试不依赖精确容量（无 full-buffer 测试），迁移安全。
- **`ConcurrentRingBuffer`（MPSC 通用封装）推迟到 M2**：M1 的两个消费者都是 SPSC 且各有定制同步（pipe 自带 Spinlock+irq_save+EOF，keyboard 是 IRQ 单生产者 + poll 单消费者）。通用 MPSC 封装对它们价值小，真正用户是 M2 的多生产者 dmesg。
- **pipe_ops 零影响**（复核确认）：`PipeReadOps/PipeWriteOps` 只调 `Pipe::read/write`（公共签名），不碰私有 `buffer_/count_`；迁移不动 InodeOps 基类。

## 陷阱

- **grep 调用方两种形态**：`->ops->op()` 箭头形态 **和** `ops_obj.op()` 点号形态（如 `test_pipe.cpp` 的 `PipeReadOps` 局部对象）都要 grep——M0 批2b 只 grep 箭头形态，漏掉点号形态靠编译才暴露。
- **多 Edit 应用过程的 IDE 诊断是中间态噪音**：同文件连续 Edit 时，IDE 会报"已删成员未定义"Error，实为部分 Edit 未反映的快照；以编译为 ground truth。
- **RingBuffer 返 bool 非 ErrorOr**：`push/pop` 用 bool/size_t 表示满空（状态非错误），与 ErrorOr 契约不冲突；pipe 对外仍是批4 的 `int64_t`/errno 边界，ErrorOr 不泄 ABI。

## 验证

- run-kernel-test **662/0**（两批各跑）
- host 单测：`test_pipe` 37/0 + `test_sys_pipe` 13/0（try 往返 / partial / 满空 / EOF / 多轮回绕全覆盖）

## 范围裁定（serial 排除）

侦察确认 `kernel/drivers/serial/` 只写硬件寄存器（`putc`），**无软件环形缓冲**，不在迁移范围。真正的次要候选只有 keyboard。

---

commit：`0746ebf`（批1 pipe）、`715a00f`（批2 keyboard）。
