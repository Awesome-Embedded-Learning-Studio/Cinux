# F-INFRA I-4 — static_assert 锁关键结构体布局

> 日期 2026-06-19 · F-INFRA Tier0 批 I-4 · 分支 `feat/finfra`

## 背景
R11：全内核仅 32 处 static_assert，SlabHeader/SlabCache/LogEntry/VMA/InterruptFrame 缺编译期布局锁。字段重排/插入会静默破坏：slab 侵入式 free list、klog 环、VMA 侵入式链表，尤其 InterruptFrame——interrupts.S 按 push 顺序建帧、C handler 用 `(RSP+offset)` 索引，**字段偏移**才是 asm ABI 契约。

## 目标
把 5 个结构体布局钉死在编译期，未来任何字段改动即时 fail build。

## 设计/决策（measure-first：尺寸从当前正确定义推算，编译期验证）
- **InterruptFrame（big + mini 各自锁，无法跨相等）**：big 是 `[[gnu::packed]]`、mini 不是，但 21×uint64 都 168B。用 **offsetof** 矩阵（r15@0 / error_code@120 / rip@128 / ss@160）+ sizeof 168——锁的是 asm 真正依赖的字段偏移。两处独立锁（big/mini 是独立可执行，无单一 TU 见过两者，无法跨断言；靠约定 + 各自 fail 保持同步）。
- **SlabHeader(40) / SlabCache(64) / LogEntry(272) / VMA(56)**：sizeof 锁。尺寸推算：LogLevel 是 `enum class : uint8_t`（1B）、VmaFlags 是 `enum class : uint64_t`（8B），据此算 LogEntry=8+1+256→272、VMA=4×u64+3×ptr+u64 flags=56。
- **延伸零警告到 mini 链接**（I-3 只清了 big_kernel）：mini 的 `MINI_KERNEL_COMMON_LINK_OPTIONS` 加 `-Wl,--build-id=none`（修 build-id 丢弃警告）+ `-Wl,--no-warn-rwx-segments`（bootstrap 单 LOAD 段 RWX 是合理的 real-mode→long-mode trampoline，抑制而非拆 linker.ld）。

## 验证
- `cmake --build build --target run-kernel-test` → 全部 static_assert 通过（尺寸推算全对）、**全目标零工具链警告**（编译/链接均零；剩 `make: Clock skew detected` 是 WSL2 时钟漂移环境噪声，非代码）、**840/0**。

## 文件
- 改：`kernel/mm/slab.hpp`、`kernel/lib/klog.hpp`、`kernel/mm/vma.hpp`、`kernel/arch/x86_64/idt.hpp`（+stddef.h）、`kernel/mini/arch/x86_64/idt.hpp`（+stddef.h）、`kernel/mini/CMakeLists.txt`（链接零警告）。
- 5 结构体全部锁定：SlabHeader=40 / SlabCache=64 / LogEntry=272 / VMA=56 / InterruptFrame=168（+4 处 offsetof，big+mini 各一套）。
