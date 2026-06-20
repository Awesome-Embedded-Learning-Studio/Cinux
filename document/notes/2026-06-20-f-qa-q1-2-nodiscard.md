# F-QA Q1-2 — ErrorOr `[[nodiscard]]` + test 忽略处理

> 里程碑：F-QA Q1（零成本门禁，批 2）。日期 2026-06-20。分支 `feat/f-qa`。基线 875/0。

## 背景与目标

给 `ErrorOr` 加 `[[nodiscard]]`，让"忘处理 `ErrorOr` 返回值"在编译期警告——对齐 Linux `refcount_t` / `expected` 的"不变量编码进类型"思路。`ErrorOr` 在 Cinux-Base 子模块 `expected.hpp`。

## 决策

1. **class 级 `[[nodiscard]]`（子模块一处）** 而非 kernel/ 44 个函数声明逐个加：DRY、覆盖全部返回点 + 未来新增、对齐"不变量编码进类型"。给 `ErrorOr<T>` + `ErrorOr<void>` 各加 `class [[nodiscard]]`（C++17 合法：attribute 可应用于 class 声明）。
2. **碰子模块**：class 级最强但 `ErrorOr` 在 Cinux-Base。本批碰子模块（`Cinux-Base` 在 `main` branch，commit 安全，不悬空）。Q4a `RefCount` 会再碰一次子模块。
3. **test 忽略（32 处）暂压制**：加 `[[nodiscard]]` 后，32 处 test fixture 忽略 `ErrorOr`（多为 setup `ext2/ramdisk->mount()` + 少量 get_page/write）触发 `-Wunused-result`。**生产 kernel 代码零忽略**（ErrorOr 铁律 M0 迁移后已守住）。尝试用 `TEST_ASSERT_TRUE` 清，但该宏失败时 `return;`（无值），**不能用在返回 `Ramdisk*`/`AhciExt2Pair` 的非 void setup helper** → 编译 error。需非 void-safe 检查原语（DEBT-016）。本批给 `big_kernel_test` + `test/` 加 `-Wno-unused-result` 临时压制（生产 `big_kernel_common` 不受影响，`[[nodiscard]]` 保护不丢）。

## 陷阱

- `[[nodiscard]]` on class 是 C++17 合法（可应用于 class 声明），GCC 接受（验证 0 syntax error，Cinux-Base `-Werror` 也没炸）。
- **`TEST_ASSERT_TRUE` 宏（big_kernel_test.h）失败时 `return;`（无值），不能用在非 void 函数**——这是 test fixture 清忽略碰到 10 个编译 error 的根因。host 框架 `ASSERT_TRUE` 同理。彻底修需 abort-based 宏（DEBT-016）。
- 生产零忽略 = ErrorOr 铁律执行得好；忽略全在 test setup（低风险，test 后续断言会暴露 setup 失败）。
- 子模块改动：`Cinux-Base` 在 `main` branch（非 detached），commit 安全；主仓库 bump submodule ref。

## 验证

- `big_kernel_test` 编译：0 warning / 0 error（`-Wno` 压制 test 忽略，生产 `big_kernel_common` 零忽略不变）。
- `timeout 40 run-kernel-test`：875 passed / 0 failed。
- host `test_shell_redirect` 编译：0 warning / 0 error（`test/` `add_compile_options(-Wno-unused-result)` 生效）。

## 产出

- `third_party/Cinux-Base/include/cinux/expected.hpp`：`ErrorOr<T>` + `ErrorOr<void>` 加 `[[nodiscard]]`（子模块 commit）。
- `kernel/CMakeLists.txt`：`big_kernel_test` PRIVATE `-Wno-unused-result`（注释指 DEBT-016）。
- `test/CMakeLists.txt`：`add_compile_options(-Wno-unused-result)`（注释指 DEBT-016）。
- `document/todo/quality/debt.md`：DEBT-016（test 忽略，D8，P2；修复 = `ASSERT_OK` 宏 + 去 `-Wno`）。
- 后续：DEBT-016 清 test 忽略；Q1-3 CI 多 config 矩阵 / Q1-4 测试项数门禁 / Q1-5 host ASAN/TSAN/gcov。
