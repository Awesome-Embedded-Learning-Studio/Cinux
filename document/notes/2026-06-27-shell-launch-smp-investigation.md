# shell 启动 musl 程序 + SMP 不稳定排查（F10 后续）

> 分支 `feat/f10-musl`，全部未 commit（SMP 不稳，绿才提交）。本文整理排查过程 + 数据 + 根因假设，供接手。
> 关联 memory：[[f10-shell-launch-smp-fork-race]] [[f10-batch6-df-investigation]]。

## 目标

让 GUI shell（点 Shell 图标）能启动 musl 程序：敲 `/hello` → shell fork+execve → musl hello 跑出 `Hello from musl on CinuxOS!`。标准 shell 行为（未知命令当程序路径启动），不是 `run` 内置命令。

## 已实现（工作树，默认 run-kernel-test 950/0 不破）

- `user/libc/syscall.{h,cpp}`：加 `sys_fork`/`sys_execve`/`sys_waitpid` 包装（shell 之前无 fork/execve 能力）。
- shell `main.cpp`：未知命令（不匹配内置）→ `launch_program` fork+execve（标准 shell fallback）；`tokenize` 加 `argv[argc]=nullptr`（**真 bug**：原不 null 终止，execve 的 copy_strvec 读越界）。
- `sys_execve`（kernel/syscall/sys_execve.cpp）：抽 `enter_loaded_program`（kernel/proc/user_launch.cpp，launch_user_program + sys_execve 共享「execve 后铺初始栈+跳」）。**修真 bug**：原 sys_execve 只 `execve()` 加载 ELF 然后 return 0，**不铺栈不跳**→ 程序没跑（批3 follow-up「sys_execve 替换路径栈铺设」）。+ execve 前**拷用户 argv/envp/path 进内核**（execve unmap 用户页后读悬空）。
- `IVMAStore::clear()` + execve 清 VMA（**Bug 1 修复**，见下）。

## Bug 1（已修）：execve 替换路径不清 VMA → -12 崩溃

**确定性 bug**。execve 有 `clear_user_mappings`（清用户页）但**不清 VMA 表**。fork 子继承 shell 的 VMA → execve 插 /hello 的 PT_LOAD VMA @0x400000 撞旧 VMA → `[EXECVE] VMA record failed for 0x400000-0x401000` → `result=-12` → 子返回用户态写 -12（CR2=0xFFFFFFFFFFFFFFF4）崩。

harness smoke 不触发（用全新 AS，无旧 VMA）。shell 是首个 execve 替换路径调用方。

**修**：`IVMAStore` 加 `virtual void clear()=0`（LinkedListVMAStore 已有 clear()，从 private 移 public + override）；execve `clear_user_mappings` 后加 `task->addr_space->vmas().clear()`。**此修复后确定性崩溃消失**，shell 启动单 CPU + -smp 2 偶发能跑出 Hello。

## Bug 2（未修，SMP 竞态）：fork 不稳定

`make run` 是 **-smp 2**。shell 启动是**首个真 user-task fork+execve 在 SMP 下的压力测试**，暴露多个潜伏 SMP 竞态。**单 CPU 稳定**（harness smoke kernel-task fork + 用户 musl fork 都过）；**-smp 2 偶发**。两种表现：

### 表现 A（log.txt）：子进程 fork 炸弹

```
shell(6) fork→tid=7(pid=2)  [FORK] dispatch ret=2 tid=6   ← 父对
tid=7 fork→tid=8(pid=3)     [FORK] dispatch ret=3 tid=7   ← tid=7(子) fork 返正值！应返 0
tid=8 execve /hello (result=0) ← tid=8(子的子) 返 0 对，execve 成功
tid=7 再 fork→tid=9 ...      ← tid=7 卡在 shell 循环反复 fork
```
子进程（tid=7）的**创建期 fork 返回正值**（应 0）→ 误当父进程 → 跳过 execve 分支 → waitpid→shell 循环→重读 stdin 的 /hello→再 fork。链式。每个孙进程（tid=8,9…）反而返 0 正确 execve /hello。

### 表现 B（log2.txt）：shell 反复 fork + DF

```
shell(6) fork→tid=7(pid=2)  execve /hello ✓
shell(6) fork→tid=8(pid=2)  execve /hello ✓   ← shell 反复 fork，全 pid=2
shell(6) fork→tid=9(pid=2)  ...
PANIC: Double Fault @ Serial::putc 的 ret（栈写花）
```
shell 反复 fork（waitpid 没阻塞？stdin 重复投递？），fork 炸弹下 2 CPU 并发 kprintf → Serial::putc 栈损坏 → DF。

### 诊断数据（已加打印，工作树）

- `[FORK] child tid=X ctx.rip=A ctx.rsp=B retaddr=C (trampoline=D)`：fork 时打子 ctx。**所有 fork 的 ctx 都正确**（ctx.rip=trampoline=0xFFFFFFFF810047FA，retaddr=0xFFFFFFFF81023550=sys_fork 的 `pop %rbp;ret`，一致）。→ 子 ctx 设对了。
- `[FORK] dispatch ret=R tid=T`：syscall_dispatch 里 SYS_fork 的返回值（父+子解栈都打）。
- `[EXECVE] ...`：sys_execve 各步。

sys_fork 反汇编（big_kernel）：`push %rbp; mov $g_pid_alloc,%rdi; mov %rsp,%rbp; call fork; 0x81023550: pop %rbp; ret`——**极小，不 reload child_pid**，rax 直接传 fork() 返回值。fork_child_trampoline=`xor %rax,%rax;ret`。

### 关键矛盾（未解）

ctx 对 + 解栈路径对（fork_child_trampoline rax=0 → sys_fork pop%rbp;ret → syscall_dispatch，rax 全程 0）→ 子**应**返 0 且经 `[FORK] dispatch` 打印。但 log.txt 里 tid=7 创建期的 `[FORK] dispatch` **缺失**（说明它没经 syscall_dispatch 尾部），且返了正值。ctx 对但解栈结果偶发错 → **疑 fork 拷的内核栈内容偶发不一致**（SMP 竞态），或 CoW 用户页竞态把子的用户态写花。

## 根因假设（按概率）

1. **CoW 页错误处理 SMP 不原子**（最可能）：parent+child 跨 CPU 同时 fault 同一 CoW 用户页 → 双重解析 → 一方拿陈旧 phys → 子用户栈/数据写花 → 子跑飞（返错值/反复 fork）。**解释 user-task fork 不稳、kernel-task fork（harness，无用户页 CoW）稳定**。→ 下一步查 CoW handler 是否 per-page 原子（锁/原子 PTE 位）。
2. fork 拷内核栈 SMP 竞态：父内核栈在 memcpy 时被并发改（但 fork 跑在父 CPU，父不并发——除非 AP 定时器 IRQ 在 fork 期间改栈？fork 是否关中断？）。
3. waitpid 阻塞语义在 SMP 下破：shell 父 waitpid 不阻塞 → shell 循环 re-fork（log2.txt）。
4. kprintf/Serial 并发不安全（DF 的直接因，但下游 of fork 炸弹）。

## 下一步（接手干 SMP）

1. **查 CoW handler 原子性**（kernel/arch/x86_64 PF handler + mm CoW 逻辑）：parent+child 同页并发 fault 是否串行。这是头号嫌疑。
2. 若 CoW OK → 查 fork 是否关中断（fork 期间 AP 定时器 IRQ 在父栈上 nest 会改栈）。fork.cpp 未见 `cli`。
3. 复现：headless 造 user-task fork under -smp 2（写个 user/libc forktest 程序，或 shell 自启模式），插桩 CoW handler 打印同页并发 fault。

## build/验证状态

- 默认 run-kernel-test **950/0**（所有改动不破默认）。
- harness smoke（-DCINUX_MUSL_HELLO_SMOKE=ON，单 CPU）musl hello 跑通。
- musl fork（clone）-smp 2 稳定（6/6）→ **clone 路径 OK，fork 路径不稳**（fork.cpp vs clone.cpp 子创建虽同代码，但 SYS_fork vs SYS_clone 调用方不同）。
- `make run`（-smp 2）shell 敲 /hello：**偶发跑出 Hello，偶发 fork 炸弹/DF**。
