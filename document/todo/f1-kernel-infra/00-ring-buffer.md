# M1: Ring Buffer Library

> 通用可复用环形缓冲区，作为 kernel/lib/ 的基础组件。
> 后续被 klog (M2)、IPC pipe 改造、serial 缓冲等复用。

> **📌 状态（2026-06-16 更新）：M1 已完成 ✅（消费迁移模式，同 M0）。**
> - 基础 `RingBuffer<T,N>` / `ByteRingBuffer<N>` 由 [Cinux-Base](../../../third_party/Cinux-Base/include/cinux/ring_buffer.hpp) 提供 → **T1/T3 跳过**（不在 kernel/ 重写）；**T4 单测** Cinux-Base 已有 `test_ring_buffer.cpp`。
> - **实际完成 = 消费迁移**：[`kernel/ipc/pipe.cpp`](../../../kernel/ipc/pipe.cpp)（批1，commit 0746ebf）+ [`kernel/drivers/keyboard/keyboard.cpp`](../../../kernel/drivers/keyboard/keyboard.cpp)（批2，commit 715a00f）两处手写环形缓冲统一复用 `cinux::lib::RingBuffer`。
> - **T2 ConcurrentRingBuffer（MPSC 通用封装）推迟到 M2 日志**：M1 的两个消费者都是 SPSC——pipe 已自带 Spinlock + irq_save + 阻塞 spin-wait + EOF 语义（外层保护，非通用 MPSC），keyboard 是 IRQ 单生产者 + poll 单消费者（InterruptGuard 保护）。两者各自的定制同步比通用 ConcurrentRingBuffer 更贴合，强行套用反而损失语义。ConcurrentRingBuffer 的真正用户是 M2 日志（多 CPU 多生产者写 dmesg，需 MPSC），届时再实现。
> - 详见 `document/ai/PLAN.md` + `document/notes/2026-06-16-f1-m1-ringbuffer-migration.md`。下方的 T1–T4 任务清单与代码示例保留作原始规划参考。

## 目标

在 `kernel/lib/` 下实现类型安全的环形缓冲区模板，支持单生产者-单消费者（SPSC）和多线程安全（MPSC）两种模式。

## 现有参考

- `kernel/ipc/pipe.cpp`：手动管理的 ring buffer index（可被本库替代）
- `kernel/proc/sync.hpp`：Spinlock 类（用于线程安全版本）

## 任务清单

### T1: RingBuffer 模板类

**文件**: `kernel/lib/ring_buffer.hpp`

```cpp
namespace cinux::lib {

template <typename T, size_t N>
class RingBuffer {
public:
    constexpr RingBuffer() = default;

    // 状态查询
    constexpr bool empty() const;
    constexpr bool full() const;
    constexpr size_t size() const;
    constexpr size_t capacity() const;

    // 操作
    constexpr bool push(const T& item);     // 成功返回 true，满则丢弃
    constexpr bool pop(T& out);             // 成功返回 true，空则失败
    constexpr void clear();

    // 批量操作
    constexpr size_t push_batch(const T* items, size_t count);
    constexpr size_t pop_batch(T* items, size_t count);

    // 只读访问
    constexpr const T& peek_front() const;
    constexpr const T& peek_back() const;

private:
    T buffer_[N]{};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
};

} // namespace cinux::lib
```

- [ ] 实现 RingBuffer<T, N> 模板（header-only）
- [ ] 所有方法 constexpr / inline
- [ ] 无动态内存分配
- [ ] power-of-2 的 N 用位运算取模优化

### T2: 线程安全版本

**文件**: `kernel/lib/ring_buffer.hpp`（同一文件）

```cpp
template <typename T, size_t N>
class ConcurrentRingBuffer {
public:
    bool push(const T& item);       // 内部加锁
    bool pop(T& out);               // 内部加锁
    size_t push_batch(const T* items, size_t count);
    size_t pop_batch(T* items, size_t count);

private:
    RingBuffer<T, N> buf_;
    cinux::proc::Spinlock lock_;
};
```

- [ ] 包装 RingBuffer + Spinlock
- [ ] push/pop 自动加锁
- [ ] 批量操作单次加锁

### T3: 特化 — ByteRingBuffer

**文件**: `kernel/lib/ring_buffer.hpp`

针对 `uint8_t` 的优化版本（klog、serial 等场景）：

```cpp
template <size_t N>
using ByteRingBuffer = RingBuffer<uint8_t, N>;
```

- [ ] 类型别名定义
- [ ] 额外提供 `write(const void* data, size_t len)` 方法
- [ ] 额外提供 `read(void* buf, size_t len)` 方法

### T4: 单元测试

**文件**: `kernel/test/test_ring_buffer.cpp`

- [ ] 基本 push/pop 顺序正确
- [ ] 满时 push 返回 false，数据不丢失
- [ ] 空时 pop 返回 false
- [ ] push_batch / pop_batch 边界条件
- [ ] clear() 后 empty() == true
- [ ] peek_front / peek_back 正确
- [ ] ConcurrentRingBuffer 多线程场景（如果测试框架支持）

## 产出物

- [ ] `kernel/lib/ring_buffer.hpp` — RingBuffer + ConcurrentRingBuffer + ByteRingBuffer
- [ ] `kernel/test/test_ring_buffer.cpp` — 单元测试
- [ ] 编译通过 + 测试通过
