# F13 visor 前置 — launch_user_program 公共化(2026-06-21)

> F13 visor 跨平台 GUI 库**第一批代码**。[visor-02 §1](../todo/f13-gui/visor-02-refactor-and-separation.md) 的 spawn 公共化前置重构。分支 `feat/f13-visor`。纯重构,行为不变。

## 背景

visor 化前,GUI shell 启动逻辑内联在两处且**高度重复**:
- [init.cpp](../../kernel/proc/init.cpp)(non-GUI 模式 fork 子进程:execve + 建用户栈 + Stack VMA + activate + jump)
- [gui_init.cpp shell_child_entry](../../kernel/gui/gui_init.cpp)(同上 + fd_table 接 pipe)

visor-02 §1:不先合并,L7 host adapter 的 spawn 就是在重写,且是迁移最易回归点。本批提取公共路径。

## 目标

提取 `launch_user_program(path, argv, envp)`,消除两处重复,为 visor L7 adapter spawn 铺路。纯重构,行为不变。

## 设计

`launch_user_program`([user_launch.{hpp,cpp}](../../kernel/proc/user_launch.hpp))= 用户程序启动「最后一公里」:
**execve → 预映 USER_STACK_PAGES → 记 demand-growth Stack VMA(F2-M5 硬门控)→ activate → jump_to_usermode**。

调用方负责:
- `addr_space = new AddressSpace()`(init fork 子进程 / gui shell_child_entry 都先 new)
- (可选)`fd_table`(gui 设 stdin/stdout pipe;non-GUI 不设,走 legacy sys_read/write)

## 决策

- **函数名 `launch_user_program`**(非 visor-02 写的 `spawn_user_process`):它不创建 task(fork/TaskBuilder 由调用方做),只「加载 + 跳」。语义更准——落地比文档更聚焦为 launch 层。
- **不标 `[[noreturn]]`**:底层 `Scheduler::exit_current` / `jump_to_usermode` 都未声明 `[[noreturn]]`;标了反而触发「noreturn function does return」类警告。靠末尾 `exit_current` 兜底(与现有 init.cpp/gui_init.cpp 模式一致)。头注释说明 never returns。
- **Stack VMA 两处合一**(init.cpp + gui_init.cpp),**不是「三处」**(visor-02 §1 笔误):[fork.cpp](../../kernel/proc/fork.cpp)/[clone.cpp](../../kernel/proc/clone.cpp) 是 fork/clone 时**继承父 VMA**(for 循环 `insert(v->flags)`,含 Stack),不新建栈 VMA,不属本批。
- **不处理 GOTCHA#22**(TaskBuilder 全局 tid 污染):那是测试基础设施问题(状态机测试用栈 Task 规避),与 spawn 公共化正交。本批不动,留 follow-up。

## 验证(全绿)

- 编译:big_kernel_test + big_kernel **零警告**(clang-format 过)
- **run-kernel-test 887/0**(回归不破;含 test_multi_terminal 等 GUI 路径,覆盖 create_shell_terminal / shell_child_entry / launch_user_program)
- **GUI 冒烟**(`cmake --build build --target run`):production 启动序列完整 — GDT/IDT/ATA/ext2/VFS → [APIC] switched → [GUI] Desktop icons registered + tick callback → gui_worker (tid=4) 启动。**无 panic**。

## GOTCHA

- **IDE IWYU warning**:gui_init.cpp 的 paging_config.hpp/usermode.hpp/pmm.hpp、init.cpp 若干 arch/mm include 替换后「not used directly」(符号移到 user_launch.cpp)。clangd 启发式,**非编译错误**(项目 -Werror 不含 IWYU,CI format 已禁用)。本批保留冗余 include(保守,避免漏判间接依赖);IWYU 清理若需要单独成批。
- **`make run` 不是 target**:CLAUDE.md 写「make run」,实际 GUI run target 是 cmake 的 `run`(`cmake --build build --target run`),顶层无 Makefile。冒烟用 cmake target。

## 下一步

visor-02 §2(visor 形状骨架:`kernel/gui/visor_core/` + Host ABI 表 + adapter 转发),或等 visor 仓库就绪。本批 Cinux 侧 spawn 公共化完成,L7 adapter spawn 将退化为薄封装(调 launch_user_program)。
