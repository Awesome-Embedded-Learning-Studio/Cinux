# CinuxOS Coding Taste

> 代码风格的**单一权威**。`DIRECTIVES.md` B 与 `CLAUDE.md`/`AGENTS.md` 指向本文。
> **铁律**：机械风格（缩进/换行/对齐/指针位置/include 排序）以 `.clang-format` 为准——**用 clang-format 格式化，不手调**；本文只管 clang-format 管不到的：命名、惯用法、注释、结构。
> 现实优先：本文扎根真实代码与 `.clang-format`，取代旧的 `document/ai_prompts/code_conventions.md`（Milestone-0 时代，已与代码脱节）。

## 0. 权威层级
1. `.clang-format`（Google 基底）— 机械风格，跑工具。
2. 本文档 — 命名/注释/惯用法/结构。
3. `DIRECTIVES.md` A — 语言级铁律（C++17、无异常、无 RTTI、Cinux-Base 契约、syscall 翻译边界）。

## 1. 语言约束（见 DIRECTIVES A）
C++17（**不用 C++20**，不用 concepts/ranges）。禁异常（→ `ErrorOr`）、禁 RTTI。freestanding 内核，禁 `<vector> <string> <memory> <iostream> <algorithm>`；要容器/字符串自实现或用 `cinux::lib`。错误一律 `cinux::lib::ErrorOr<T>`。

## 2. 命名

**文件**：`snake_case.hpp` / `snake_case.cpp`；syscall 实现用 `sys_<name>.cpp`；测试 `test_<module>.cpp`；汇编 `snake_case.S` / `.h`。

| 元素 | 规则 | 示例 |
|------|------|------|
| 类/结构/联合/枚举类 | PascalCase | `PageAllocator`、`TaskState` |
| 函数/变量/参数 | snake_case | `alloc_page()`、`free_page_count` |
| 私有成员 | snake_case + 尾随 `_`（**必须**） | `total_pages_`、`bitmap_` |
| 公开结构体字段 | snake_case，无后缀 | `inode`、`offset`、`flags` |
| 命名空间 | 小写简短 | `mm`、`fs`、`proc`、`drivers`、`arch`、`cinux::lib` |

**常量与枚举值（目标态 = k 前缀，迁移中）**：
- `constexpr`/`const` 常量 → `kPascalCase`：`kPageSize`、`kKernelBaseAddr`。
- `enum class` 值 → `kPascalCase`：`kRunning`、`kReady`。
- 宏（`#define`）→ `UPPER_SNAKE`：`MAX_TESTS`、`ROUND_UP(x, align)`。
- **迁移状态**：现存大量 `UPPER_SNAKE` 常量（`PAGE_SIZE`、`PT_ENTRIES`）与纯 PascalCase 枚举值（`Running`、`Regular`）属 legacy。**新代码一律 k 前缀**；**大幅触碰某 legacy 常量/枚举时顺手迁移**；**不擅自批量重写**（守 DIRECTIVES L3）。
- 例外：`cinux::lib::Error` 枚举（`OutOfMemory`、`NotFound`…）在 Cinux-Base **子模块**（另一仓库），保持纯 PascalCase，**不动**。

## 3. 注释（一律英文）
- **文件头**：Doxygen `/** @file path @brief ... */`，说明模块职责。
- **公开 API**：Doxygen `@brief`/`@param`/`@return`/`@note`。
- **行内**：`//` 英文，解释**为什么**而非是什么。
- **TODO/FIXME/XXX**：**必须带实现思路**（不只是 `// TODO`），例：
  ```cpp
  // TODO: 实现 free_page：1) 校验 4KB 对齐 2) index=phys/4096 3) 查 bitmap 4) 清位 5) 递增 free 计数
  ```
- **迁移**：现存中文注释（kernel 内极少）在触碰时改英文。

## 4. 格式化（clang-format 权威）
`.clang-format` 要点（不要与之对抗）：
- 缩进 4 空格、K&R 大括号（左括号同行）、`ColumnLimit: 100`。
- **命名空间内容不缩进**（`NamespaceIndentation: None`）。
- **指针/引用左贴**：`T* p`、`const char* p`、`void* x`（**唯一**风格，`PointerAlignment: Left`）。
- 连续赋值/声明/宏**对齐**（工具自动）。
- 成员初始化列表：单行 `: head_(0), tail_(0), count_(0)`；超长才换行缩进。
- **include 分块重排**（`IncludeBlocks: Regroup`）：先对应 `.hpp`（.cpp 里）→ C 系统 `<...>` → C++ `<...>` → 项目 `"..."`；`SortIncludes: CaseSensitive`。
- 条件 include 用 `#ifdef CINUX_GUI / #    include "..." / #endif`（`#`后缩进）。
- **提交前跑 clang-format**；CI 也跑。

## 5. ErrorOr 惯用法（真实模式）
```cpp
// 成功：隐式返回值，或 ErrorOr<void> 用 {}
cinux::lib::ErrorOr<Inode*> FileSystem::lookup(StringView path);
return inode;            // 成功（隐式构造 ErrorOr<Inode*>）
return Error::NotFound;  // 失败
return {};               // ErrorOr<void> 成功

// 调用方：先判 ok()，失败 log + 返回本层错误，成功取 .value()
auto parent = fs->lookup(parent_path);
if (!parent.ok()) {
    kprintf("[SYS] lookup failed: %s\n", path);
    return Error::NotFound;   // 或在 syscall 边界 return -ENOENT
}
Inode* dir = parent.value();
```
- **边界**：内核内部全 `ErrorOr`；syscall trap 入口翻成 `int`/errno（`Error→errno` 映射表批4 补）。
- 勿把 `-1` 之类裸值包成"成功"——适配层（如 `PipeReadOps`）须把底层 `-1` 显式翻译成 `Error`。

## 6. 类 / 结构布局
- `struct` = POD/聚合（公开字段，如 `CpuContext`、`File`）；`class` = 有封装（私有状态 + 方法）。有非平凡不变量/私有态用 `class`。
- 顺序：`public` 类型与方法在前，`private` 成员在后；访问修饰符之间 1 空行。
- 成员优先**类内初始化**：`uint64_t count_ = 0;`、`void* fs_private{nullptr};`。
- `[[nodiscard]]` 标分配/工厂；`[[noreturn]]` 标 `kpanic`。
- 用 `using` 别名简化（`ByteSpan = Span<uint8_t>`）。

## 7. 头文件
- **kernel 头**：`#pragma once`。
- **Cinux-Base（子模块）头**：传统 `#ifndef/#define/#endif` 守卫（另一仓库，不改）。
- 用前向声明（`struct InterruptFrame;`）削减编译依赖。

## 7b. 文件行数（500 行软上限）
- 源文件（`.cpp`/`.hpp`）**软上限 500 行**——PR review 会标红超限。一个文件一个聚焦职责。
- 写/改完一个文件后 `wc -l` 看一眼；超 500 **及时按职责拆**（如 `fork.cpp` 拆出 `clone.cpp`、`process.hpp` 拆出 `shared_cwd.hpp` 或把 execve/waitpid 声明挪到独立头）。不要堆到 PR 被打回再拆。
- 测试文件可适当放宽（聚合器性质），但亦尽量按子系统拆。

## 8. 断言 / panic
- **编译期**：`static_assert`（结构体布局不变量，如 `static_assert(sizeof(CpuContext) == 80)`）。
- **运行期**：`kprintf` 诊断 + `kpanic(fmt, ...)`（`cli; hlt` 死循环）。`__assert_fail`（`kernel/arch/x86_64/crt_stub.cpp`）转发到 `kpanic`，所以 `<cassert>` 的 `assert()` 在 freestanding 下也可用（经 crt_stub）。
- 不依赖 libc 的 `abort`。

## 9. 测试
框架 `kernel/test/big_kernel_test.h`：
```cpp
namespace test_ext2_readdir {
void test_readdir_dot_and_dotdot() {
    TEST_ASSERT_GT(root->ops->readdir(root, 0, name, sizeof(name)), 0);
}
}  // namespace test_ext2_readdir

extern "C" void run_ext2_tests() {
    TEST_SECTION("Ext2 (028)");
    RUN_TEST(test_ext2_readdir::test_readdir_dot_and_dotdot);
    TEST_SUMMARY();
}
```
- 断言宏：`TEST_ASSERT_EQ / NE / TRUE / FALSE / GT / LT`。
- 测试函数按主题入 `namespace test_<topic> {}`；入口 `extern "C" void run_<module>_tests()`。
- host 单测 `test/unit/`；真实内核测试 `cmake --build build --target run-kernel-test -j$(nproc)`。
- **注意**：`test/unit/test_syscall_ext2.cpp` 是自包含 mock（不 link kernel 源码），接口改动不影响它；`test/unit/test_vfs_mount.cpp` link 真 vfs_mount.cpp，要跟着改。

## 10. 汇编（AT&T）
源/目顺序 `mov src, dst`；寄存器 `%rax`、立即数 `$0x1000`、寻址 `offset(base, index, scale)`。每条指令右侧给语义注释；函数头用 `# ===` 块注明职责/输入(`%rdi`)/输出/影响寄存器。

## 11. CMake
变量/宏大写下划线（`CMAKE_CXX_STANDARD`、`CINUX_DEBUG`），目标小写下划线（`cinux_kernel`、`libc`）。区域标题用 `# ===` 块；`# TODO` 带意图。

## 12. 提交前 checklist
- [ ] 跑过 clang-format；命名合规（PascalCase/snake_case/`_`/`k`前缀目标）
- [ ] 注释英文；新公开 API 有 Doxygen；TODO 带思路
- [ ] 无异常/RTTI/禁用标准库头；错误走 `ErrorOr`
- [ ] syscall 边界 `ErrorOr` 未泄漏到用户 ABI
- [ ] kernel 头 `#pragma once`；include 分块正确
- [ ] 编译 + `run-kernel-test -j$(nproc)` 全绿
- [ ] commit message 纯描述（无 Co-Auth、无里程碑标签）
