# CinuxBase — Design Specification

> 零 OS 耦合的 C++ 基础类型库，为 Cinux 内核及未来项目提供 header-only、constexpr、无堆分配的核心组件。

## 1. 项目约束

| 约束 | 要求 |
|------|------|
| C++ 标准 | C++17（不使用 C++20 feature） |
| 命名空间 | `cinux::lib` |
| 内存模型 | 全部 header-only，无动态内存分配（`new` / `malloc` / `::operator new` 禁止出现） |
| constexpr | 所有可在编译期计算的函数标记 `constexpr` |
| 异常 | 禁止使用 C++ 异常（`throw` / `try` / `catch`），错误通过 `ErrorOr<T>` 传播 |
| RTTI | 禁止使用 RTTI（`dynamic_cast` / `typeid`） |
| 依赖 | 零外部依赖，不 include 任何非标准库头文件 |
| 可包含的标准头 | `<cstddef>` `<cstdint>` `<cstdarg>` `<type_traits>` `<utility>` `<cstring>` |
| 禁止包含 | `<vector>` `<string>` `<memory>` `<iostream>` `<algorithm>` |
| 文件大小 | 每个 `.hpp` 文件 ≤ 400 行（含注释） |
| 代码风格 | Google C++ Style Guide 变体：4 空格缩进，`snake_case` 函数/变量，`PascalCase` 类型 |

## 2. 组件清单与依赖关系

```
Array<T,N> ──────────────────────┐
Span<T> ─────────────────────────┤
StringView ──────────────────────┼──→ 无内部依赖（叶子节点）
BufferView + StaticBuffer<N> ────┤
ErrorOr<T> ──────────────────────┘
        │
        └──→ RingBuffer<T,N> ──→ ByteRingBuffer<N>
                                      │
                                      └──→ Logger（Sink 回调模式）
```

引入顺序：叶子组件先行，Logger 最后。

---

## 3. 组件详细设计

---

### 3.1 ErrorOr\<T\> — 通用错误处理

**文件**: `include/cinux/expected.hpp`

替代裸 `int` 错误码和 `errno` 模式。Value/Error 判别式联合体。

```cpp
namespace cinux::lib {

enum class Error : uint32_t {
    Ok = 0,
    OutOfMemory,
    InvalidArgument,
    NotFound,
    IOError,
    AlreadyExists,
    PermissionDenied,
    WouldBlock,
    BufferOverflow,
    NotImplemented,
    BrokenPipe,
    ConnectionRefused,
    TimedOut,
    Busy,
};

// 将 Error 转为人类可读字符串（constexpr）
constexpr const char* error_string(Error e);

template <typename T>
class ErrorOr {
public:
    // 成功路径
    constexpr ErrorOr(T value);
    // 错误路径
    constexpr ErrorOr(Error err);

    // 拷贝 / 移动（T 必须支持）
    constexpr ErrorOr(const ErrorOr&) = default;
    constexpr ErrorOr(ErrorOr&&) = default;
    constexpr ErrorOr& operator=(const ErrorOr&) = default;
    constexpr ErrorOr& operator=(ErrorOr&&) = default;

    // 状态查询
    constexpr bool ok() const;
    constexpr explicit operator bool() const;

    // 值访问 — ok()==false 时调用触发 assert / panic
    constexpr T& value();
    constexpr const T& value() const;
    constexpr T& operator*();
    constexpr const T& operator*() const;
    constexpr T* operator->();
    constexpr const T* operator->() const;

    // 错误访问
    constexpr Error error() const;

private:
    union {
        T value_;
        Error error_;
    };
    bool is_ok_;
};

// ErrorOr<void> 特化 — 只判成功/失败，无值
template <>
class ErrorOr<void> {
public:
    constexpr ErrorOr();          // 成功
    constexpr ErrorOr(Error err); // 失败

    constexpr bool ok() const;
    constexpr explicit operator bool() const;
    constexpr Error error() const;

private:
    Error error_;
    bool is_ok_;
};

} // namespace cinux::lib
```

**测试要求**:
- 成功路径：`value()` 返回正确值，`ok()` == true
- 错误路径：`error()` 返回正确错误码，`ok()` == false
- `ErrorOr<void>` 成功和失败两条路径
- `error_string()` 覆盖所有枚举值
- 拷贝 / 移动语义正确

---

### 3.2 StringView — 零分配字符串视图

**文件**: `include/cinux/string_view.hpp`

不持有数据，不假设 `\0` 终止，纯长度语义。

```cpp
namespace cinux::lib {

class StringView {
public:
    // 构造
    constexpr StringView() = default;
    constexpr StringView(const char* str);              // strlen 遍历
    constexpr StringView(const char* str, size_t len);  // 显式长度

    // 状态
    constexpr size_t size() const;
    constexpr bool empty() const;
    constexpr const char* data() const;

    // 元素访问 — 越界行为：返回 '\0'
    constexpr char operator[](size_t i) const;
    constexpr char front() const;
    constexpr char back() const;

    // 比较
    constexpr bool equals(StringView other) const;
    constexpr bool starts_with(StringView prefix) const;
    constexpr bool ends_with(StringView suffix) const;
    constexpr int compare(StringView other) const;

    // 运算符
    constexpr bool operator==(StringView other) const;
    constexpr bool operator!=(StringView other) const;
    constexpr bool operator<(StringView other) const;
    constexpr bool operator<=(StringView other) const;
    constexpr bool operator>(StringView other) const;
    constexpr bool operator>=(StringView other) const;

    // 查找 — 未找到返回 npos
    constexpr size_t find(char c, size_t pos = 0) const;
    constexpr size_t find(StringView needle, size_t pos = 0) const;
    constexpr size_t rfind(char c) const;

    // 子串
    constexpr StringView substr(size_t pos, size_t count = npos) const;

    static constexpr size_t npos = static_cast<size_t>(-1);

private:
    const char* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace cinux::lib
```

**测试要求**:
- 空视图：`empty()` == true，`size()` == 0
- C 字符串构造和显式长度构造
- `starts_with` / `ends_with` 含空前缀/后缀
- `find` / `rfind` 找到和未找到
- `substr` 边界：pos > size、count 超出尾部
- 比较运算符全排列

---

### 3.3 Span\<T\> — 非拥有连续内存视图

**文件**: `include/cinux/span.hpp`

```cpp
namespace cinux::lib {

template <typename T>
class Span {
public:
    constexpr Span() = default;
    constexpr Span(T* data, size_t size);
    constexpr Span(T* begin, T* end);

    // C 数组构造
    template <size_t N>
    constexpr Span(T (&arr)[N]);

    // 状态
    constexpr size_t size() const;
    constexpr bool empty() const;
    constexpr T* data() const;

    // 元素访问 — 越界行为：debug assert
    constexpr T& operator[](size_t i) const;
    constexpr T& front() const;
    constexpr T& back() const;

    // 子视图
    constexpr Span first(size_t count) const;
    constexpr Span last(size_t count) const;
    constexpr Span subspan(size_t pos, size_t count = npos) const;

    // 迭代
    constexpr T* begin() const;
    constexpr T* end() const;

    static constexpr size_t npos = static_cast<size_t>(-1);

private:
    T* data_ = nullptr;
    size_t size_ = 0;
};

// 常用别名
using ByteSpan = Span<uint8_t>;
using ConstByteSpan = Span<const uint8_t>;

} // namespace cinux::lib
```

**测试要求**:
- 指针+长度、指针对、C 数组三种构造
- `first` / `last` / `subspan` 边界
- range-for 迭代正确
- 空 span 的 `empty()` == true
- `const T` 特化确保只读（编译期检查）

---

### 3.4 Array\<T, N\> — 固定大小容器

**文件**: `include/cinux/array.hpp`

```cpp
namespace cinux::lib {

template <typename T, size_t N>
class Array {
public:
    // 元素访问
    constexpr T& operator[](size_t i);
    constexpr const T& operator[](size_t i) const;
    constexpr T& front();
    constexpr const T& front() const;
    constexpr T& back();
    constexpr const T& back() const;
    constexpr T* data();
    constexpr const T* data() const;

    // 状态
    constexpr size_t size() const;
    constexpr bool empty() const;  // N==0 时 true

    // 迭代
    constexpr T* begin();
    constexpr const T* begin() const;
    constexpr T* end();
    constexpr const T* end() const;

    // 操作
    constexpr void fill(const T& value);

    // 比较
    constexpr bool operator==(const Array& other) const;
    constexpr bool operator!=(const Array& other) const;

private:
    T data_[N]{};
};

} // namespace cinux::lib
```

**测试要求**:
- 聚合体初始化 `Array<int, 3> a = {1, 2, 3}`
- `fill()` 批量填充
- `operator==` / `operator!=`
- range-for 迭代
- `Array<int, 0>` 边界情况
- `size()` 编译期可计算（`static_assert`）

---

### 3.5 BufferView + StaticBuffer\<N\>

**文件**: `include/cinux/buffer.hpp`

```cpp
namespace cinux::lib {

// 非 owning 只读字节视图
class BufferView {
public:
    constexpr BufferView() = default;
    constexpr BufferView(const void* data, size_t size);

    constexpr const uint8_t* data() const;
    constexpr size_t size() const;
    constexpr bool empty() const;

    constexpr BufferView slice(size_t offset, size_t len) const;
    constexpr const uint8_t& operator[](size_t i) const;

    // 便捷：转 StringView（不含 \0）
    constexpr StringView as_string() const;

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

// 固定大小 owning 缓冲区（栈上分配）
template <size_t N>
class StaticBuffer {
public:
    constexpr StaticBuffer() = default;

    constexpr uint8_t* data();
    constexpr const uint8_t* data() const;
    constexpr size_t capacity() const;   // N
    constexpr size_t size() const;       // 当前有效长度
    constexpr bool empty() const;
    constexpr void resize(size_t new_size);  // new_size <= N，否则 assert

    constexpr void fill(uint8_t value);
    constexpr void copy_from(const void* src, size_t len);
    constexpr void copy_to(void* dst, size_t len) const;

    // 桥接
    constexpr BufferView view() const;
    constexpr ByteSpan as_span();
    constexpr ConstByteSpan as_span() const;

private:
    uint8_t data_[N]{};
    size_t size_ = 0;
};

} // namespace cinux::lib
```

**测试要求**:
- `BufferView::slice` 越界截断
- `StaticBuffer::copy_from` / `copy_to` 数据正确
- `resize` 超过 capacity 时 assert
- 桥接：`view()` / `as_span()` 类型正确
- `as_string()` 正确转换

---

### 3.6 RingBuffer\<T, N\> — 通用环形缓冲区

**文件**: `include/cinux/ring_buffer.hpp`

单生产者-单消费者模型，无锁设计（非线程安全版本）。

```cpp
namespace cinux::lib {

template <typename T, size_t N>
class RingBuffer {
public:
    constexpr RingBuffer() = default;

    // 状态
    constexpr bool empty() const;
    constexpr bool full() const;
    constexpr size_t size() const;
    constexpr size_t capacity() const;  // N

    // 单元素操作
    constexpr bool push(const T& item);     // 满返回 false
    constexpr bool pop(T& out);             // 空返回 false
    constexpr void clear();

    // 批量操作
    constexpr size_t push_batch(const T* items, size_t count);
    constexpr size_t pop_batch(T* items, size_t count);

    // 只读访问
    constexpr const T& peek_front() const;  // 空时 undefined
    constexpr const T& peek_back() const;   // 空时 undefined

private:
    T buffer_[N]{};
    size_t head_{0};
    size_t tail_{0};
    size_t count_{0};
};

// uint8_t 特化别名
template <size_t N>
using ByteRingBuffer = RingBuffer<uint8_t, N>;

} // namespace cinux::lib
```

**实现注意**:
- 当 `N` 为 2 的幂时，取模可用位运算 `& (N - 1)` 优化
- `push_batch` / `pop_batch` 需处理环绕（wrap-around），即一次 memcpy 不够时需要两次
- 不使用 `static_assert(N > 0)` 之外的约束

**测试要求**:
- 基本 push/pop FIFO 顺序
- 满 push 返回 false，数据不丢失
- 空 pop 返回 false
- `push_batch` / `pop_batch` 环绕边界
- `clear()` 后 `empty()` == true
- `peek_front` / `peek_back` 正确
- N = 1 边界情况

---

### 3.7 Logger — 轻量日志框架

**文件**: `include/cinux/logger.hpp`

Logger 只做三件事：**级别过滤**、**格式化**、**分发到 Sink**。不持有任何输出设备。

```cpp
namespace cinux::lib {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
};

// 级别转字符串
constexpr const char* log_level_string(LogLevel level);

// Sink 回调：接收格式化后的完整消息
// - level: 本条日志的级别
// - message: 格式化后的完整字符串（以 '\0' 结尾）
// - ctx: 用户注册时的上下文指针
using LogSink = void(*)(LogLevel level, const char* message, void* ctx);

class Logger {
public:
    // 单例（或独立实例，由使用方决定）
    static Logger& instance();

    // 也可创建独立实例（用于测试或多个子系统）
    Logger() = default;

    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 配置
    void set_level(LogLevel level);
    LogLevel level() const;

    // Sink 管理 — 最多注册 MAX_SINKS 个
    static constexpr int MAX_SINKS = 8;
    void register_sink(LogSink sink, void* ctx = nullptr);
    void clear_sinks();

    // 核心日志方法
    void log(LogLevel level, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // 便捷方法 — 内联级别检查，未启用时零开销
    void debug(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void info(const char* fmt, ...)  __attribute__((format(printf, 2, 3)));
    void warn(const char* fmt, ...)  __attribute__((format(printf, 2, 3)));
    void error(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // 统计
    size_t dropped_count() const;    // 因级别过滤跳过的条数
    void reset_dropped_count();

private:
    // 内部格式化 + 分发
    void emit(LogLevel level, const char* fmt, va_list args);

    LogLevel threshold_{LogLevel::INFO};
    struct SinkEntry {
        LogSink fn = nullptr;
        void* ctx = nullptr;
    };
    SinkEntry sinks_[MAX_SINKS]{};
    int sink_count_{0};
    size_t dropped_{0};
};

} // namespace cinux::lib
```

**格式化输出规范**:

Logger 内部格式化为：`[LEVEL] message\n`

| LogLevel | 输出前缀 |
|----------|----------|
| DEBUG | `[DEBUG] ` |
| INFO | `[INFO] ` |
| WARN | `[WARN] ` |
| ERROR | `[ERROR] ` |

内部缓冲区大小：256 字节（`LOGGER_MSG_MAX`）。超出部分截断。

**测试要求**:
- Sink 回调接收到正确格式化的消息
- 级别过滤：`set_level(WARN)` 后 `debug()` / `info()` 不触发 sink
- `dropped_count()` 正确递增
- 多个 sink 同时注册时都能收到
- `clear_sinks()` 后不再触发
- 格式化正确：`%s`、`%d`、`%u`、`%x`、`%p`
- 消息超过 256 字节时安全截断（不越界）

**内核集成方式**（供参考，不属于 CinuxBase）:

```cpp
// kernel/lib/klog.cpp — 注册内核 sink
void kernel_log_sink(LogLevel level, const char* msg, void*) {
    // 1. 推入 ConcurrentRingBuffer（M2 KernelLog）
    // 2. 输出到 serial port
}
// kernel 启动时调用：
Logger::instance().register_sink(kernel_log_sink);
```

---

### 3.8 IBlockDevice — 块设备抽象接口

**文件**: `include/cinux/block_device.hpp`

纯虚接口。实现在消费方（内核驱动、测试 mock）。

```cpp
namespace cinux::lib {

class IBlockDevice {
public:
    virtual ~IBlockDevice() = default;

    // 同步块读写 — buf 为虚拟地址，实现内部负责 DMA 映射
    virtual bool read_blocks(uint64_t block, uint64_t count, void* buf) = 0;
    virtual bool write_blocks(uint64_t block, uint64_t count, const void* buf) = 0;

    // 设备信息
    virtual uint64_t block_count() const = 0;
    virtual uint64_t block_size() const = 0;

    // 刷新写缓存 — 默认空操作
    virtual void flush() {}
};

} // namespace cinux::lib
```

**测试要求**:
- 用 mock 实现验证接口可被继承和调用
- RAMBlockDevice 式的内存实现可用于集成测试

---

## 4. 文件清单汇总

| 文件路径 | 组件 | 估计行数 |
|----------|------|----------|
| `include/cinux/expected.hpp` | Error 枚举 + ErrorOr\<T\> + ErrorOr\<void\> | ~120 |
| `include/cinux/string_view.hpp` | StringView | ~150 |
| `include/cinux/span.hpp` | Span\<T\> + ByteSpan + ConstByteSpan | ~120 |
| `include/cinux/array.hpp` | Array\<T, N\> | ~100 |
| `include/cinux/buffer.hpp` | BufferView + StaticBuffer\<N\> | ~120 |
| `include/cinux/ring_buffer.hpp` | RingBuffer\<T,N\> + ByteRingBuffer\<N\> | ~140 |
| `include/cinux/logger.hpp` | LogLevel + Logger + LogSink | ~120 |
| `include/cinux/block_device.hpp` | IBlockDevice 接口 | ~30 |
| **总计** | 8 个文件 | **~900 行** |

## 5. 测试文件汇总

| 文件路径 | 测试内容 |
|----------|----------|
| `tests/test_expected.cpp` | ErrorOr 成功/错误路径、void 特化、拷贝移动 |
| `tests/test_string_view.cpp` | 构造、比较、查找、子串、运算符 |
| `tests/test_span.cpp` | 构造、子视图、迭代、别名 |
| `tests/test_array.cpp` | 聚合初始化、fill、比较、迭代 |
| `tests/test_buffer.cpp` | BufferView slice、StaticBuffer copy/fill、桥接 |
| `tests/test_ring_buffer.cpp` | push/pop FIFO、满/空、batch、clear |
| `tests/test_logger.cpp` | sink 注册、级别过滤、格式化、多 sink |
| `tests/test_block_device.cpp` | Mock 实现验证接口可用 |

## 6. CMake 结构

```cmake
cmake_minimum_required(VERSION 3.16)
project(CinuxBase LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only library
add_library(cinuxbase INTERFACE)
target_include_directories(cinuxbase INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Tests
option(CINUXBASE_BUILD_TESTS "Build tests" ON)
if(CINUXBASE_BUILD_TESTS)
    enable_testing()
    # 使用系统自带的或其他测试框架，这里以最简单的 assert 方式为例
    # 或使用 Google Test / Catch2
    add_subdirectory(tests)
endif()
```

## 7. 消费方接入方式

Cinux 内核通过 CMake FetchContent 或 add_subdirectory 引入：

```cmake
# kernel/CMakeLists.txt
FetchContent_Declare(
    cinuxbase
    GIT_REPOSITORY https://github.com/Charliechen114514/CinuxBase.git
    GIT_TAG        stable
)
FetchContent_MakeAvailable(cinuxbase)

target_link_libraries(kernel PUBLIC cinuxbase)
```

内核中直接使用：

```cpp
#include <cinux/expected.hpp>
#include <cinux/string_view.hpp>
#include <cinux/logger.hpp>

ErrorOr<void> open_file(StringView path) {
    if (path.empty()) return Error::InvalidArgument;
    CINUX_INFO("opening %.*s", (int)path.size(), path.data());
    return {};
}
```
