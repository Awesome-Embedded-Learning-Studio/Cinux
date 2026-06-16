# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。
> **F1-M1 = RingBuffer 消费迁移 ✅ 完成（2026-06-16）**。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

## ✅ M1（RingBuffer 消费迁移）已完成 — 2026-06-16

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | pipe 内部存储 → `RingBuffer<char,4096>` push_batch/pop_batch（保留 Spinlock+irq_save+阻塞+EOF） | ✅ | 0746ebf | 662/0 + host 50/0 |
| 批2 | keyboard 事件队列 → `RingBuffer<KeyEvent,64>` push/pop（drop-newest + InterruptGuard 保留） | ✅ | 715a00f | 662/0 |
| 收尾 | 文档(本文+ROADMAP+DEVLOG) + 全量 run-kernel-test | ✅ | (本次) | 662/0 |

Cinux-Base 的 `cinux::lib::RingBuffer<T,N>` 早已就绪（freestanding header-only），内核此前在 pipe/keyboard 各手写一套同款环形缓冲。本里程碑消灭重复、回归「kernel 消费 cinux::lib」层化铁律——同 M0 的消费迁移模式，非造轮子。pipe 保留外层锁/阻塞/EOF 语义；keyboard drop-newest 语义不变（容量 63→64，不再牺牲槽位）。批表全 ✅，662/0 验证通过。

**下一焦点待定**：F1-M2 日志（自然延续）或另行 `/milestone` 拆批。本文进入待命——新里程碑启动时重写本节。

## OPEN GOTCHAS（跨里程碑通用，活警告）
1. **验证 target**：内核改动用 run-kernel-test（QEMU 真内核 ~662 项）；host 单测（`test/unit/`）不在其中，改被 mock 类后 push 前补 `cmake --build build` 全量或 `make test_host`（L5）。但 **test_keyboard 是 QEMU in-kernel 测试**（big_kernel_test.h 框架，在 662 内），别误当 host。
2. **Cinux-Base 是子模块**：容器/类型在 `third_party/Cinux-Base/include/cinux/*.hpp`，`#include <cinux/xxx.hpp>` 即用；勿在 kernel/ 重写。当前就绪：ErrorOr/StringView/Span/Buffer/RingBuffer/IntrusiveList/StaticHashMap…（`Array<T,N>` 仍未提供）。
3. **里程碑定义区分「类型就绪」vs「内核消费」**（M0/M1 教训）：ROADMAP 的 ⏳ 易被误读为"待实现"，实则 Cinux-Base 类型常已就绪、待办是 kernel/ 消费迁移。写里程碑描述要标清（如「类型就绪+消费迁移」格式）。
4. **grep 调用方两种形态**：`->ops->op()` 箭头 **和** `ops_obj.op()` 点号（如 test_pipe.cpp 的 `PipeReadOps` 局部对象），都 grep。
5. **多 Edit 应用过程的 IDE 诊断是中间态噪音**（批2 教训）：同文件连续 Edit 时，IDE 会报"已删成员未定义"Error，实为部分 Edit 未反映的快照；以编译为 ground truth。
