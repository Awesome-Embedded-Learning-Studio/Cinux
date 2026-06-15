# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。改前读「OPEN GOTCHAS」。
> **F1-M0 = ErrorOr 消费迁移**：类型已由 Cinux-Base 提供（✅），本里程碑是把内核代码迁到使用 `ErrorOr`。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

## 批表

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | FileSystem::mount/lookup → ErrorOr<void>/ErrorOr<Inode*> | ✅ | 93e2870 | 662/0 ×2 |
| 批2a | InodeOps::create/mkdir/unlink/stat → ErrorOr | ✅ | 93e2870 | 662/0 ×2 |
| 批2b | InodeOps::read/write/readdir → ErrorOr<int64_t> | 🔄 NEXT | — | — |
| 批3 | proc 子系统 ErrorOr 化 | ⏳ | — | — |
| 批4 | syscall 边界 ErrorOr→int 翻译(errno 映射) | ⏳ | — | — |
| 收尾 | 文档(本文+document/todo) + 全量 run-kernel-test | ⏳ | — | — |

最近提交：`93e2870`(2026-06-15) `refactor(fs): 引入 ErrorOr 替换 VFS/InodeOps 裸错误码 (M0 批1+批2a)`，30 files +317−223。

## NEXT — 批2b：read/write/readdir → ErrorOr<int64_t>
范围：read/write 返回 `ErrorOr<int64_t>`(字节数或错误)；readdir 同改，**消除三态歧义**。
实现：ext2_common.cpp(Ext2FileOps::read/write, Ext2DirOps::readdir)、ramdisk.cpp(RamdiskFileOps::read/write, RamdiskDirOps::write/readdir)。调用方 7 处：execve.cpp×3(read)、sys_read、sys_write、sys_getdents、sys_rmdir。测试 test/unit/ + run-kernel-test。
完成门：`cmake --build build --target run-kernel-test -j$(nproc)` 全绿。提交：`refactor(fs): read/write/readdir 迁移 ErrorOr<int64_t>`，无 Co-Auth。

## OPEN GOTCHAS
1. **readdir 三态歧义**：现返回 正数=一个条目 / `0`=读完(正常) / `-1`=错误。改 ErrorOr 后 `sys_getdents`/`sys_rmdir` 循环终止条件须逐处核对，别把"读完"误判成错误。
2. **验证 target 易混**：只用 run-kernel-test。
3. **Cinux-Base 是子模块**：勿在 kernel/ 重写；`Array<T,N>` 尚未提供。
4. **批4 是翻译层**：ErrorOr 不泄到用户 ABI。

## M0 基础设施笔记（跨批 2b/3/4 复用）
- 测试 helper `kernel/test/big_kernel_test.h`：`lookup_or_null`/`create_or_null`/`mkdir_or_null`/`unlink_rc` 把 ErrorOr 降级回 nullptr/0-1 以适配旧 `TEST_ASSERT_*`；批量改造用 Python 正则。
- `__assert_fail` 在 `kernel/arch/x86_64/crt_stub.cpp`（ErrorOr::value() 的 `<cassert>` 依赖；freestanding 无 libc）。
- host test 加 Cinux-Base include：`test/CMakeLists.txt` 的 `TEST_INCLUDE_DIRS` 与 `add_cinux_integration_test` 两处都要加。
- `test/unit/test_syscall_ext2.cpp` 是自包含 mock（自实现 syscall + mock FS/InodeOps，不 link kernel 源码），接口改动不影响它，勿误改；`test/unit/test_vfs_mount.cpp` link 真 vfs_mount.cpp，要跟着改。
