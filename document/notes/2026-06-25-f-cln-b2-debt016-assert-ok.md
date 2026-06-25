# F-CLN 批2 — DEBT-016 test fixture ErrorOr 忽略关债 — 2026-06-25

> DEBT-016（test fixture 忽略 ErrorOr 返回值，32 处）闭环。加 ASSERT_OK 宏 + 清忽略 + 去 -Wno-unused-result。

## ASSERT_OK 宏（非 void-safe）
两套 framework 各加 `ASSERT_OK(expr)`——检查 ErrorOr `.ok()`，失败标记 + 强制终止（非 void-safe，不能 `return;`，故 abort 整个 run，区别于 TEST_ASSERT 的 `return;`）：
- [big_kernel_test.h](../../kernel/test/big_kernel_test.h)：失败 `io_outb(0xf4, 1)`（QEMU isa-debug-exit，exit code 3）+ while(1)
- [test_framework.h](../../test/framework/test_framework.h)：失败 `_TEST_ABORT()`（host: abort()，QEMU: hlt）

## 清忽略点（32 处）
- **big_kernel_test 29 处**：test_ramdisk 17（`rd.mount();`）+ test_ext2 系 4（`result.ext2->mount();` 链式）+ vfs/cwd_stat/file_mmap/syscall_ext2/shell_write/ahci_write 各（mount）+ test_page_cache:237（`cache.get_page(&ino, 0);` 故意丢弃触发填充）。sed 批量改简单对象 mount + 手改链式 `result.ext2->mount` + get_page。
- **host 3 处**：test_shell_redirect:205-207（`f1->inode->ops->write(...)` 3 处）

## 去 -Wno-unused-result
- `test/CMakeLists.txt:16` 全局 `-Wno-unused-result` 删除（注释标记）
- `kernel/CMakeLists.txt:299` big_kernel_test `-Wno-unused-result` 删（保留 -Wno-frame-larger-than，批1 加的）

## 验证
- big_kernel_test 编译：**零 ignoring + 零 error**
- test_host 编译：**零 ignoring + 零 error**
- run-kernel-test **931/0** 绿（29 处 ASSERT_OK 不意外 abort）
- host ctest **54/0**（100%，test_shell_redirect 9 passed）

## 产出
- big_kernel_test.h + test_framework.h（ASSERT_OK 宏）+ 10 个 test cpp（32 处包 ASSERT_OK）+ 2 CMakeLists（去 -Wno）+ debt.md DEBT-016 ✅ + PLAN 批2 ✅

下个：批3 DEBT-018 kMaxCpus 统一。
