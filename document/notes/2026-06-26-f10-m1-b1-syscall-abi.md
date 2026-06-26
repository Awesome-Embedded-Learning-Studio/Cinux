# F10-M1 批1 — syscall 号纠偏 + 错误返回约定收尾 + sys_pipe RAII 化

> 里程碑：F10-M1 用户态运行时 / musl 静态移植（批1）。分支 `feat/f10-musl`，commit `7a9f1e6`。
> 前置：批0 立项 docs（`eaa60ca`）。验证：`run-kernel-test` 945/0 + 全量 `cmake --build build` 绿。

## 背景与目标

musl 移植要求内核 syscall ABI 对齐 Linux x86_64——musl 直接发原始 syscall，期望内核返
**负 errno**、用 **Linux 号**、读 **Linux 结构体布局**。批1 只管前两件（号 + 返回约定），
结构体布局留批2，auxv 留批3。

调研先读了码，结论比预想乐观：

- **返回约定基本已是 Linux 风格**：`sys_open` 返 `-to_errno(...)`（`kernel/errno.hpp::to_errno`），
  就是 musl 要的"负 errno"。只有几个老 syscall 返裸 `-1`。
- **syscall 号几乎全对**：全表核对，除一处外都和 Linux x86_64 一致。唯一错的是 `SYS_chdir`。

## 改动

### 1. SYS_chdir 撞号修正（真 bug）

`syscall_nums.hpp` 里 `SYS_chdir = 12` 和 `SYS_brk = 12` **撞号**。`syscall.cpp` 注册时
chdir 在前（line 84）、brk 在后（line 99），brk **覆盖**了 slot 12 → chdir 实际不可达，
shell 的 `cd` 命令发 syscall 12 命中的是 `sys_brk`（坏的）。Linux 正确号 chdir=80、brk=12。
改成 `SYS_chdir = 80`，全表其余号核对无误。

用户侧 `user/libc/syscall.cpp` 不硬编码号，而是 `#include "kernel/syscall/syscall_nums.hpp"`
直接用 `SyscallNr::SYS_chdir`——**单一事实源**，改 enum 一处内核+用户壳自动同步，只需全量
重编（`user_shell` 会重编重嵌）。

### 2. 返回约定收尾（裸 `-1` → `-errno`）

`kernel/syscall/` 里残留的裸 `return -1`，按场景补成正确的负 errno：

| 文件:场景 | 改成 |
|---|---|
| getpid / getppid（current()==null，实际不可达）| `-kEsrch` |
| getcwd 坏用户地址（null / 非规范 / 内核态）| `-kEfault` |
| getcwd size==0 | `-kEinval` |
| getcwd current()==null | `-kEsrch` |
| getcwd 缓冲区太小 | `-kErange` |
| sys_pipe 坏地址 | `-kEfault` |
| sys_pipe fd 表满 | `-kEmfile` |

`errno.hpp` 补 `kErange = 34`（POSIX ERANGE）——getcwd 缓冲区太小时 Linux 返它，musl 的
`getcwd` 靠 ERANGE 判断要不要扩缓冲。

### 3. sys_pipe RAII 化（用户授权的 scope 加项）

用户看到失败路径的手动 `delete` 级联（5 个对象逐个删）觉得吓人，问能不能 RAII。授权后做。

**先摸清所有权链**（RAII 前必做，否则 double-free）：

- `FDTable::close(fd)` 只释放 **File**（fd 条目），**不碰 Inode**。
- `File` / `Inode` / `InodeOps` 都是持裸指针的 plain struct，**互相不拥有、不删除**。
- 所以 pipe 的 `Inode / ops / Pipe` 在正常 close 时**根本没人释放**（hobby-OS 已知限制，
  不是批1 的事）——**唯一删它们的地方就是 sys_pipe 的失败路径**。

结论：失败路径 `close(read_fd)` 后 `delete read_inode` 是自洽的（close 只删 File，inode 仍归
本函数），没 double-free。因此可以安全 RAII——`std::unique_ptr` + 成功路径 `release()`，
**失败自动全删、成功原样移交给 FDTable**，行为不变，级联 delete 消失。

关键：`read_inode` 的 `release()` 要推迟到**两个 fd 都成功后**——write-fd 失败时要
`close(read_fd)` 撤销读端分配，这时 `read_inode` 还得由 unique_ptr 持着才能被释放。

## 决策与教训

### ⚠️ 别手搓 UniquePtr——用 std::unique_ptr

我一开始写了 `kernel/lib/unique_ptr.hpp`（freestanding move-only 手搓版），header 注释写
的理由是"freestanding without `<memory>`"。**用户一句"为什么不 std::unique_ptr"点醒了我**——
这跟本里程碑"不自建 libc 生态"的原则**自相矛盾**，我又造了个轮子。

实测换 `std::unique_ptr`：**全量编译 + 链接全过**，零 undefined symbol。理由站不住：

- `<memory>` 是 **freestanding 可用**头（header-only，不依赖宿主运行时）；
- 内核到处用 `new`/`delete`，`operator new`/`operator delete` 已提供，而 `std::unique_ptr`
  默认 deleter 就是调 `delete`；
- `-fno-exceptions` 对 `unique_ptr` 无影响；
- DIRECTIVES 禁 `<memory>` 是针对 **Cinux-Base 子模块**（无堆），**不是 kernel/**。

**教训**："别造轮子"同样适用于标准库智能指针。别因为"freestanding 内核"就条件反射式禁 std——
先实测标准库能不能编链，能就用。手搓头删掉，commit amend 抹出历史。

## 陷阱

- **`std::unique_ptr` 成功路径必须 `release()`**：否则析构时把已经移交给 FDTable 的对象又删一遍 → double-free。
- **`read_inode` 不能早 release**：要等 write_fd 也成功；write-fd 失败路径还要靠它被自动释放。
- **测试文件命名空间**：`test_sys_pipe.cpp` 在全局命名空间（不在 `cinux::` 内），`kEfault` 要写
  `cinux::kEfault`；而 `sys_pipe.cpp` 在 `namespace cinux::syscall` 里能裸用 `kEfault`（向上查找）。
  改完 IDE 报"undeclared identifier"才注意到，加限定。
- **chdir 撞号是潜伏 bug**：test 没覆盖（cd 走 brd 不报错只是无效），靠 musl 号核对才暴露。

## 验证

- 全量 `cmake --build build -j$(nproc)`：绿（含 user_shell 重编重嵌）。
- `timeout 40 cmake --build build --target run-kernel-test`：**945/0**（含 chdir 修正后 cd 恢复可用；
  test_sys_pipe 两处断言从 `-1` 改 `-cinux::kEfault` 跟进）。
- clang-format 跑过。

## 下一步

批2：Linux 结构体布局——`sys_stat` 对齐 Linux x86_64 stat(144B)、`UserSigAction` 对齐 Linux
sigaction、sigset / iovec。这是 musl 静态二进制不读错字段的第二根支柱。
