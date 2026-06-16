# 2026-06-15 F1-M0 ErrorOr 消费迁移 — 无异常错误处理全链路

## 背景

CinuxOS 的铁律：**禁异常**（throw/try/catch），错误经 `cinux::lib::ErrorOr<T>`（Cinux-Base 子模块提供）。类型层（`ErrorOr`/`Error` 枚举）已就绪，但 kernel/ 的消费方仍大量用裸 `int`/`-1`/nullptr 表达错误——三态歧义（正数/0/-1）、错误信息丢失、与无异常铁律脱节。

本里程碑把内核错误处理迁到 `ErrorOr`，同后续 M1/M2 的模式：**类型已就绪，工作是消费迁移**。

## 目标

全链路无异常错误处理：
- 内核内部一律 `ErrorOr<T>`
- 仅 syscall trap 入口翻成用户态 `int`/errno（`ErrorOr` 不泄用户 ABI）

## 实现（4 批）

### 批1+2a — VFS / InodeOps 引入 ErrorOr（commit 93e2870）

`FileSystem::mount/lookup` → `ErrorOr<void>`/`ErrorOr<Inode*>`；`InodeOps::create/mkdir/unlink/stat` → `ErrorOr`；ramdisk/ext2 实现 + init/execve + ~14 syscall 消费方跟进。先改 VFS 接口与写/元数据 ops。

### 批2b — read/write/readdir → ErrorOr<int64_t>（commit 6d47c99）

ext2_common.cpp / ramdisk.cpp 的 `read/write/readdir` 改返 `ErrorOr<int64_t>`；7 个调用方跟进。`value==0` 作 OK（目录读完）、`!ok` 作错误——压掉裸 int 三态歧义。留单独一批因 readdir 的"读完"三态需小心处理。

### 批4 — syscall 边界 Error → errno（commit a81536a）

新建 [`kernel/errno.hpp`](../../kernel/errno.hpp)：`cinux::to_errno(Error)` 全 14 变体映射 + `kPascalCase` POSIX errno 常量（freestanding，不引 libc `<errno.h>`）。10 个 FS syscall handler 失败路径由 `return -1` 改为 `return -to_errno(r.error())` 或具体 `-errno`（EFAULT/EBADF/ENOENT/EMFILE/ENOTDIR/ENOTEMPTY…）。28 处测试断言 `== -1` → `< 0`。

## 关键决策

- **批3 侦察后并入批4**：proc 无高价值 ErrorOr 目标——
  - `execve`/`waitpid` 已是结构化 errno-enum，保真高于 `ErrorOr<void>+Error`（`Error` 枚举缺 ENOEXEC/EISDIR/ECHILD/ESRCH）；
  - `fork` 错误全平凡（6×OOM + 1×invariant），且 ErrorOr 会撞 `fork_child_trampoline` 的 rax 锻造（见陷阱）；
  - `handle_cow_fault` 是谓词非错误通道。
  
  故 M0 收口落在 syscall 边界：批1/2a/2b 让 FS 返 ErrorOr，批4 把 `Error` 经 `to_errno` 翻成 `-errno` 接回用户态——此前批1/2a/2b 的投资在边界被压成 `-1` 丢弃。

## 陷阱

- **grep 调用方两种形态**：`->ops->op()` 箭头 **和** `ops_obj.op()` 点号（test_pipe.cpp 局部 `PipeReadOps` 对象）。批2b 只 grep 箭头形态，漏点号形态靠编译暴露。`PipeReadOps`/`PipeWriteOps` 也是 InodeOps 子类差点漏改。
- **fork 迁 ErrorOr 撞 `fork_child_trampoline`**：现 `xorq %rax,%rax` 锻造子进程 fork()=0（int）；改 `ErrorOr<int>` 后 rax=0 使 `is_ok_=0`→child 误判失败，须改 `movq $0x100000000,%rax` 锻造 `{value=0,ok}`——asm 耦合 C++ 布局，纯成本零收益。故 proc 边界留 errno 层。
- **errno 双源**：`cinux::proc::errno_values`（process.hpp，~8 常量）与批4 新建的 `to_errno` 表是两套。批4 让 `to_errno` 自包含（自带全 POSIX errno 常量），不动 `errno_values`/不碰 proc 测试；两源归一是可选后续清理。
- **grep 受影响测试断言须不限变量名**：`[a-z_]*` 会漏带数字变量名（`r0/r1/result2`，sys_write 测试用）。正解：grep 全部 `-1` 断言再逐条分类。

## 验证

run-kernel-test **662/0**（批4 clang-format 前后各一）。M0 至此收口。

## 基础设施沉淀（跨批复用）

- 测试 helper（`kernel/test/big_kernel_test.h`）：`lookup_or_null`/`create_or_null`/`mkdir_or_null`/`unlink_rc` 把 ErrorOr 降级回 nullptr/0-1 以适配旧 `TEST_ASSERT_*`。
- `__assert_fail` 在 `kernel/arch/x86_64/crt_stub.cpp`（ErrorOr::value() 的 `<cassert>` 依赖；freestanding 无 libc）。
- host test 加 Cinux-Base include：`test/CMakeLists.txt` 的 `TEST_INCLUDE_DIRS` 与 `add_cinux_integration_test` 两处都要加。
