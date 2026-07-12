# Cinux Coding Taste

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

## 13. 分层边界 + 最简表达（两类典型反例，易在赶进度时犯）

> 来源：F5-M5 HID 鼠标（2026-06-23）「梭哈」写快了被当面纠正。OS 这种长期项目，**解耦边界 + 最简表达不能为赶进度让步**——后面拆/改费劲。两条都进了反面教材。

### 13a. 通用/传输类不得混入应用语义

一个**按职责命名的类**只承担它名字说的那一层。**传输层**（「这个设备怎么收发字节」）绝不能长出**应用层**的知识（「这是个鼠标 / 报告怎么解码」）。判定法：把这个类换一个用途，它的字段/方法还说得通吗？说不通的就是泄漏。

**反例**（已修）：`XhciSlot` 是通用「xHCI 设备槽」传输层，一度被塞进 `mouse_ep_dci_` / `report_buf_` / `poll_mouse_report()` / `set_protocol()`——一个 USB 传输类不该知道「鼠标」。换了键盘/网卡这些字段就没意义 → 说明是应用语义泄漏。

**正确分层**：传输类只给原语；应用语义收进专门的驱动类。
```
drivers/usb/    = 纯传输：xhci_controller/slot（控制传输、add_interrupt_endpoint、poll_interrupt_in 返回原始字节）
drivers/mouse/  = 鼠标应用：UsbMouse（哪个 EP 是鼠标、报告缓冲、HID 解码），用 XhciSlot 做传输；Mouse（光标 sink）
```
依赖**单向**：应用 → 传输，绝不反向。文件也按子系统收拢（`mouse/` 装所有鼠标，别散在 `drivers/` 顶层 + `usb/` 两处）。

### 13b. 位布局重合就用 bitmask，别写「算恒等」的条件链

写「把 A 的位映射到 B」之前，**先核两边位布局是否一致**；一致就直接 bitmask，不要套逐位三元链模板——那种链既啰嗦又经常在算恒等（读者还要逐位验证才发现是 no-op）。

**反例**（已修）：HID boot mouse byte0（bit0/1/2 = left/right/middle）与 PS/2 Packet0（`LEFT=0x01/RIGHT=0x02/MIDDLE=0x04`）**位完全重合**，却写了：
```cpp
// 反例：三行算的是恒等映射（每项 == buttons & 那一位）
uint8_t mapped = ((buttons & 0x01) ? LEFT : 0) |
                 ((buttons & 0x02) ? RIGHT : 0) |
                 ((buttons & 0x04) ? MIDDLE : 0);
```
**正确**：核过重合 → 一行，且加注释说明为何不用映射：
```cpp
// HID boot-mouse byte0 uses the SAME button bit layout as PS/2 Packet0
// (bit0=left / 1=right / 2=middle) -- no per-bit remapping needed.
uint8_t mapped = buttons & (LEFT_BTN | RIGHT_BTN | MIDDLE_BTN);
```
推而广之：位运算期望值写进测试前，自己用常量重算一遍（别凭「2<<10」直觉——`1<<10=0x400`、`2<<10=0x800`，搞反过）。

## 14. 编译期开关（`#ifdef`）——开关归 CMake，源码不读半截路

> 来源：GUI 的 `#ifdef CINUX_GUI` 渗透进 init/PIT/keyboard/main 四处（2026-06-25 审查），读代码读到一半撞 `#else` 要脑补两路。OS 这种长期项目，**别让 `#ifdef` 长在别人函数的肚子里**——后面读/改都费劲。

**核心**：编不编某功能，是 CMake 的事；源码读起来该是「全编进去也读得通」，不该让审查者为了读个函数去猜哪段编不编。

**四条规矩**：
1. **可选功能整个搬进它自己的文件**，CMake 决定编不编（`if(CINUX_USB) target_sources(...)`）。别处要调它就一句普通调用；没编时给个**空壳文件**顶上（同函数名，啥也不干），让链接器选一份 → 调用处零 `#ifdef`。
2. **小诊断开关**（lockdep 这种「要不要加点检查」）包成一个宏，调用处就一行，看不见 `#ifdef`。
3. `#ifdef` 只用在两个地方：头守卫（`#pragma once` 那种）、真是两个程序（host 测试 vs 真内核）。
4. **最忌讳**：函数读到一半 `#ifdef` 分叉两条路（带 `#else`）。能消就消——收进独立文件 + 空壳，或重构让分支消失。

**反例**（init.cpp，本批清理）：`kernel_init_thread` 读到一半分叉两条路：
```cpp
#ifdef CINUX_GUI
    ... 启动桌面 ...
#else
    ... 不启桌面的另一套 ...
#endif
```
读者要在脑子里同时装两条线，到 `#endif` 才喘气。

**正例**（USB，已对）：
```cmake
# drivers/CMakeLists.txt —— 文件级 gate
if(CINUX_USB) target_sources(usb_init.cpp) else target_sources(usb_stub.cpp) endif()
```
```cpp
// 别处（init/main）—— 一句普通调用
usb::init();   // 没编 USB 时链到 usb_stub 的空壳，啥也不干
```
调用处零 `#ifdef`，读起来就是一条直线。

**一句话**：开关的归属在 CMake，不在源码。可选的东西要么整文件 + 空壳，要么宏包起来；别让 `#ifdef` 长在别人函数的肚子里。
