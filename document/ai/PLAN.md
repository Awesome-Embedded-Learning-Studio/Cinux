# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。改前读「OPEN GOTCHAS」。
> **F1-M0 = ErrorOr 消费迁移**：类型已由 Cinux-Base 提供（✅），本里程碑是把内核代码迁到使用 `ErrorOr`。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

## 批表

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | FileSystem::mount/lookup → ErrorOr<void>/ErrorOr<Inode*> | ✅ | 93e2870 | 662/0 ×2 |
| 批2a | InodeOps::create/mkdir/unlink/stat → ErrorOr | ✅ | 93e2870 | 662/0 ×2 |
| 批2b | InodeOps::read/write/readdir → ErrorOr<int64_t> | ✅ | 6d47c99 | 662/0 |
| 批3 | ~~proc ErrorOr 化~~ 侦察：无高价值目标（见 NEXT） | ⏭️ 并入批4 | — | — |
| 批4 | syscall 边界 Error→errno 翻译（接回 userspace） | ✅ | a81536a | 662/0 |
| 收尾 | 文档(本文+ROADMAP+todo+DEVLOG) + 全量 run-kernel-test | ✅ | — | 662/0 |

最近提交：`a81536a`(2026-06-15) `refactor(syscall): 引入 to_errno，syscall 失败返回 -errno 而非 -1 (M0 批4)`，16 files（新 `kernel/errno.hpp` + 10 handler + 4 test）。本次收尾 doc-sync（ROADMAP/todo/DEVLOG）commit hash 待回填。

## ✅ M0（ErrorOr 消费迁移）已完成 — 2026-06-15
FS 层（批1/2a/2b）返回 ErrorOr；syscall 边界（批4，`a81536a`）经 `to_errno` 把 `Error→errno` 接回用户态；proc 边界类型（execve/waitpid/fork）按批3 侦察结论保留 errno-encoded 形态，不强行 ErrorOr 化。批表全 ✅，662/0 验证通过（`a81536a` 即 clang-format 后复验树，HEAD 已确认无回归）。

**下一焦点待定**：F1-M1 RingBuffer（自然延续）或另行 `/milestone` 拆批。本文（PLAN）进入待命——新里程碑启动时重写本节。

## 批4 落地纪要
- 新建 `kernel/errno.hpp`：`cinux::to_errno(Error)` 全 14 变体映射 + `kPascalCase` POSIX errno 常量（freestanding，不引 libc `<errno.h>`）。
- 10 个 FS handler 的失败路径由 `return -1` 统一改为：ErrorOr 失败 `return -to_errno(r.error())`，非 ErrorOr 路径按 POSIX 语义赋值（ptr→`-kEfault`、bad fd→`-kEbadf`、not-found→`-kEnoent`、fd 满→`-kEmfile`、not-dir→`-kEnotdir`、dir 非空→`-kEnotempty`…）。
- execve/waitpid/fork 边界保持 errno-encoded 枚举/值（不经 `to_errno`）；sys_close/sys_pipe 等 非-FS-ErrorOr handler 本批未动（其 `return -1` 与对应测试保留）。
- 受影响测试断言 `== -1` → `TEST_ASSERT_LT(..., 0)`（test_vfs_syscall 18 处、test_cwd_stat 4 处、test_syscall 4 处、test_shell 2 处）；sys_close 的断言保留 `== -1`。

## OPEN GOTCHAS
1. **readdir 三态歧义**：现返回 正数=一个条目 / `0`=读完(正常) / `-1`=错误。改 ErrorOr 后 `sys_getdents`/`sys_rmdir` 循环终止条件须逐处核对，别把"读完"误判成错误。 ✅ **批2b 已验证**：value==0 作 OK(读完)、`!ok` 作错误；sys_getdents/sys_rmdir 终止条件逐处核对通过，662 测试全绿。
2. **验证 target 易混**：只用 run-kernel-test。
3. **Cinux-Base 是子模块**：勿在 kernel/ 重写；`Array<T,N>` 尚未提供。
4. **批4 是翻译层**：ErrorOr 不泄到用户 ABI。
5. **grep 调用方须覆盖两种形态**：`->ops->op(obj,…)` 指针形态 **和** `ops_obj.op(…)` 对象点号形态（如 `test_pipe.cpp` 的 `read_ops.read(…)` 局部 PipeReadOps 对象）。批2b 侦察只 grep 了箭头形态，漏掉点号形态，靠编译才暴露——下次先两种都 grep。
6. **`pipe_ops` 是 PLAN 易漏项**：`PipeReadOps`/`PipeWriteOps` 也是 `InodeOps` 子类，改基类签名必同步改（否则 override 编译断）。适配层须把 `Pipe::read/write` 的 `-1` 翻译成 `Error`，不可直接 `return pipe_->read()`（否则 -1 被包成 OK-with-(-1)）。
7. **批3 侦察结论（为何并入批4）**：proc 无高价值 ErrorOr 迁移目标——execve/waitpid 已用结构化 errno-enum（保真高于 `ErrorOr<void>+Error`，因 `Error` 枚举缺 ENOEXEC/EISDIR/ECHILD/ESRCH），fork 错误全平凡（6×OOM+1×invariant，sentinel `-1` 已够且 ErrorOr 反而压掉 7 条区分性 kprintf），`handle_cow_fault` 是谓词非错误通道。**且 fork 迁 ErrorOr 会撞 `fork_child_trampoline`**：现 `xorq %rax,%rax` 锻造子进程 fork()=0（int），改 `ErrorOr<int>` 后 rax=0 使 `is_ok_=0`→child 误判失败，须改 `movq $0x100000000,%rax` 锻造 `{value=0,ok}`——asm 耦合 C++ 布局，纯成本零收益。故 proc 边界类型留批4 syscall errno 层统一处理。
8. **批4 errno 双源**：`cinux::proc::errno_values`（process.hpp，~8 常量，2 个 test 引用）与批4 新建的 `to_errno` 表是两套。批4 让 `to_errno` 自包含（自带全 POSIX errno 常量），**不动** `errno_values`/不碰 proc 测试；两源归一是可选后续清理，不在 M0 范围。
9. **grep 受影响测试断言须不限变量名**（批4 教训）：改 handler 返回值后，找受影响测试断言别用 `[a-z_]*` 之类限定变量名的正则——会漏掉带数字的变量名（`r0`/`r1`/`result2`，正好是 sys_write 测试用的）。正解：`grep -rn "TEST_ASSERT_[A-Z]*(.\{0,60\}, *-1)" kernel/test/*.cpp` 列出**全部** `-1` 断言，再逐条分类（changed handler 改 `< 0`，dispatch/Pipe/Ramdisk/InodeOps-helper/sys_pipe/proc-枚举保留）。批4 首轮漏掉 `r0/r1` 导致 4 failed，第二轮补 grep 才补齐。

## M0 基础设施笔记（跨批 2b/3/4 复用）
- 测试 helper `kernel/test/big_kernel_test.h`：`lookup_or_null`/`create_or_null`/`mkdir_or_null`/`unlink_rc` 把 ErrorOr 降级回 nullptr/0-1 以适配旧 `TEST_ASSERT_*`；批量改造用 Python 正则。
- `__assert_fail` 在 `kernel/arch/x86_64/crt_stub.cpp`（ErrorOr::value() 的 `<cassert>` 依赖；freestanding 无 libc）。
- host test 加 Cinux-Base include：`test/CMakeLists.txt` 的 `TEST_INCLUDE_DIRS` 与 `add_cinux_integration_test` 两处都要加。
- `test/unit/test_syscall_ext2.cpp` 是自包含 mock（自实现 syscall + mock FS/InodeOps，不 link kernel 源码），接口改动不影响它，勿误改；`test/unit/test_vfs_mount.cpp` link 真 vfs_mount.cpp，要跟着改。
