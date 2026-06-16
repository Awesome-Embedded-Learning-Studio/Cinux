# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。改前读「OPEN GOTCHAS」。
> **F1-M1 = RingBuffer 消费迁移**：`cinux::lib::RingBuffer<T,N>` 类型已由 Cinux-Base 提供（✅，freestanding header-only），本里程碑把 kernel/ 里手写的环形缓冲/事件队列迁到复用它——同 M0 模式（类型就绪→内核消费）。**不是**从零写 RingBuffer。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿（改被 mock 类另补 host 全量，见 L5）。

## 批表

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | pipe 内部存储迁移：`buffer_/head_/tail_/count_` + 两段回绕 → `RingBuffer<char,4096>` 的 push_batch/pop_batch（保留 Spinlock + irq_save + 阻塞 spin-wait + EOF） | ✅ | 0746ebf | 662/0 + host 50/0 |
| 批2 | keyboard 事件队列迁移：`KeyEvent[KEY_QUEUE_SIZE]` + `head_` + enqueue/poll → `RingBuffer<KeyEvent,N>` | ⏳ | — | — |
| 收尾 | 文档(本文+ROADMAP+todo+DEVLOG) + 全量 run-kernel-test | ⏳ | — | — |

## 范围裁定
- **范围内**：pipe（核心，`kernel/ipc/pipe.{hpp,cpp}`）、keyboard（次要，`kernel/drivers/keyboard/`）。
- **范围外**：serial（侦察排除——只写硬件寄存器，无软件环形缓冲）；Cinux-Base 本体只读不改（RingBuffer 已就绪；若迁移中暴露 API 缺口如 memcpy 批量，另立子模块改动评估）。

## OPEN GOTCHAS
1. **验证 target**：内核改动用 run-kernel-test；**test_pipe.cpp / test_sys_pipe.cpp 是 host 单测**（`test/unit/`，mock FS 不跑真内核），run-kernel-test 不编译它们——改 Pipe 后 push 前补 `cmake --build build -j$(nproc)` 全量或 `make test_host`（L5，批2b 教训）。
2. **Cinux-Base 是子模块**：RingBuffer 已在 `third_party/Cinux-Base/include/cinux/ring_buffer.hpp`，`#include <cinux/ring_buffer.hpp>` 即用；勿在 kernel/ 重写。
3. **RingBuffer 返 bool 非 ErrorOr**：push/pop/push_batch/pop_batch 用 bool/size_t 表示满空（状态非错误），与 ErrorOr 契约不冲突；pipe 对外仍 int64_t/errno 边界（批4 已定），ErrorOr 不泄 ABI。
4. **批1 只换存储层，语义逐条保真**：write/read 的 `irq_save` + spin-wait(`PIPE_SPIN_WAIT_ITERS`) + EOF(reader/writer open 标志) 必须原样保留；两段回绕拷贝由 push_batch/pop_batch 接管。read 返回的 `writer_open_ ? 0 : 0`（两分支皆 0）是历史形态，保留不"修"。
5. **pipe_ops 不受影响**（复核 ✅）：`PipeReadOps`/`PipeWriteOps` 只调 `Pipe::read/write`（公共签名不变），不碰 `buffer_/count_`；迁移不动 InodeOps 基类。
6. **grep 调用方两种形态**：`->ops->op()` 箭头 **和** `ops_obj.op()` 点号（如 test_pipe.cpp 的 `PipeReadOps` 局部对象），都 grep（M0 批2b 教训）。

## M1 基础设施笔记
- RingBuffer API（已就绪）：`push/pop`（单元素 bool）、`push_batch(const T*,n)/pop_batch(T*,n)`（返 size_t，正确回绕）、`empty/full/size/capacity`、`clear`、`peek_front/back`、别名 `ByteRingBuffer<N>`。用 `count_` 区分满空（不牺牲槽位，与原 pipe 策略一致）。
- RingBuffer 非线程安全——pipe 仍由外层 `cinux::proc::Spinlock` + irq_save 保护；keyboard 由 IRQ 单生产者 / poll 单消费者。
- 性能：push_batch/pop_batch 逐元素赋值（vs 原 pipe 手写两段拷贝）；4KB 字节流编译器大概率优化为 memcpy，且 pipe 有 spin-wait、拷贝非瓶颈。实测回归再考虑 Cinux-Base memcpy 特化。
