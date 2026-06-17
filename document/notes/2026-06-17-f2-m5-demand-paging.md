# F2-M5: Demand Paging 硬门控

> 日期 2026-06-17 · 里程碑 F2-M5 · 分支 `feat/f2-m5-demand-paging`
> PF handler 加 VMA 硬门控——用户态 not-present PF 无 VMA 命中 → 真 segfault 终止进程，兑现 M1 记账价值。

## 背景

M1 批4 给 PF handler 加了 VMA `find()` 诊断，但未命中只 `klog_warn` 然后**继续映射零页**（容错）——NULL 解引用、野指针、栈溢出都被静默容错，VMA 记账沦为死数据。M5 把它升级为硬门控：用户态合法 demand PF（ELF 段/堆/栈/mmap）命中 VMA 照常服务，无 VMA 命中 → 真 segfault（杀进程）。

## 设计

- **硬门控判定**（[exception_handlers.cpp](../../kernel/arch/x86_64/exception_handlers.cpp) `handle_pf`）：not-present user PF，`vma == nullptr` 时区分——**真实 user-mode fault**（`err & 0x04`，ring3 触发）→ `klog_error` + `Scheduler::exit_current()` 终止进程；**kernel-mode 访问用户地址**（ring0 test 代码 / `copy_to_from_user`，`!(err & 0x04)`）→ 保持零页容错（不杀，保护 kernel-test PF 注入与用户访问 helper）。命中合法 VMA → demand 不变（匿名零页 / M4 文件 page cache）。
- **segfault 终止 = `exit_current()`**（[scheduler.cpp](../../kernel/proc/scheduler.cpp)）：`context_switch(&prev->ctx, &next->ctx)` 切走后执行流跳到 next 上次被切走处，**不返回 PF handler 栈帧**——prev（被杀进程）的中断帧被整体抛弃，下次 iret 不发生。等价 SIGSEGV-killed（F3 信号未做的临时方案）。`fatal_halt()` 兜底（defensive，不达）。
- **栈增长 VMA**（[init.cpp](../../kernel/proc/init.cpp) / [gui_init.cpp](../../kernel/gui/gui_init.cpp)）：Stack VMA 从 `[USER_STACK_TOP-16KB, TOP)` 扩到 `[USER_STACK_TOP-1MB, TOP)`（`USER_STACK_GROWTH=1MB`，[usermode.hpp](../../kernel/arch/x86_64/usermode.hpp)）。仅顶 16KB 预分配，余 demand-page 向下增长。VMA 底（TOP-1MB）以下无 VMA → segfault（栈溢出 guard）。execve `clear_user_mappings` 只清页表不清 VMA store → Stack VMA 经 fork 继承 / execve 复用 AS 保留，传播到所有用户程序。

## 关键决策

1. **segfault 终止用 `exit_current`（非"标记+延迟"）**：批1 两种方式 spike 后定。`exit_current` 的 `context_switch` 抛弃 prev 中断帧切 next，技术上可行且最简；"标记 Dead + 返回退出"反而复杂（标记后 iret 重执行 faulting 指令 → PF 死循环，需伪造中断帧）。
2. **user-mode 判定靠 `err & 0x04`**：这是 test 豁免的天然分界——kernel-test 是 ring0 访问用户地址（`err&0x04=0`），真实用户程序是 ring3 fault（`err&0x04=1`）。硬门控只杀后者，前者保持容错。
3. **栈增长 1MB**（propose 确认）：16KB 固定栈会让深调用栈程序（init/gui/shell）硬门控后崩；1MB 增长空间够典型程序（Linux 默认 ulimit 8MB）。VMA 底外即 guard（隐式，非显式 guard page）。
4. **只做"无 VMA→segfault"**（propose 确认）：写只读 / 执行权限违规留后续（NX 因 NXE 未启用留 F9，GOTCHA #10）。M5 聚焦地址合法性门控。

## 陷阱

- **PF handler 调 `exit_current` 的 user-mode 判定**：必须 `err & 0x04` 才杀——否则 kernel-test（ring0 访问无 VMA 用户地址，如 test_file_mmap Test B 模式）会被误杀 → 测试 hang。task==nullptr（boot context）也不杀（无 task 可切，fallback 零页）。
- **栈 VMA 必须扩大否则自爆**：硬门控上了但栈 VMA 仍 16KB → 深调用栈用户程序栈 PF 落 VMA 外 → segfault。批1（门控）+ 批2（栈）必须配套，单上批1 实机会崩（run-kernel-test 不跑真实用户深栈故 730/0 绿，掩盖此依赖）。
- **execve 复用 AS 不重建栈**：execve `clear_user_mappings` 清栈页映射但不清 VMA，Stack VMA 保留 → 子程序首次用栈 PF → demand 零页重建。若 clear_user_mappings 清了 VMA，execve 后栈访问必 segfault（M5 验证它不清）。
- **run-kernel-test 盲区**：kernel test 全是 kernel-mode fault（`err&0x04=0`），**不覆盖** user-mode segfault/demand 路径。user-mode 路径靠 make run 实机（GUI 启动到桌面不崩）+ run-kernel-test 的 shell 测试（`test_shell_*` 经 fork/execve 跑 shell 命令）间接覆盖。

## 验证

- run-kernel-test：730/0（批1/批2 回归，数不变；硬门控不破坏现有 demand 路径——test 是 kernel-mode fault 走容错，有 VMA 的 demand 照常服务）。
- 全量 build（含 host test）：编译过（CI 对等，未改公共接口）。
- 实机冒烟（`make run`，GUI 模式 headless VNC）：启动 ext2→VFS→Multi-Terminal→GUI Desktop→Mouse→Desktop icons→PIT tick 到桌面，**无 segfault/panic/halt/triple fault**（仅 `#BP` 测试断点，非致命）。
- 全路径代码分析：所有合法 demand PF 有 VMA 覆盖（execve ELF 段 / Heap / Stack / mmap 匿名与文件 / fork CoW 走 present 分支不受 not-present 门控影响）。

## 遗留

- **SIGSEGV 信号交付**（→F3）：当前 `exit_current` 直接杀（等价 SIGKILL），无信号处理 / core dump / waitpid status 码。
- **NX 强制**（→F9）：非 exec 页不能设 NX（NXE 未启用，GOTCHA #10），"执行不可执行 VMA"无法靠 PTE 强制。
- **权限违规门控**（写只读 VMA → segfault）：M5 只做无 VMA 门控，`Write` flag 权限留后续。
- **segfault 进程资源清理**：`exit_current` 只标 Dead + 切走，AddressSpace / VMA / 页表 / page cache ref 不释放（leak，现有 exit 路径固有问题）——待 task exit cleanup 里程碑。
