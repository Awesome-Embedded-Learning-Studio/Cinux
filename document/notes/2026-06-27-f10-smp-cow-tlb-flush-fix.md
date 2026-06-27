# SMP fork CoW 竞态修复 + syscall RBP ABI 修复（F10-M2）

> 分支 `feat/f10-musl`。接 [[2026-06-27-shell-launch-smp-investigation]]。
> shell（user task）在 -smp 2 下 fork+execve 启动 musl 程序偶发 fork 炸弹/#DF。本文记录根因 + 修复 + 复现器实证。

## 根因 1（主因，已修）：fork/clone CoW 改写父页表后不刷 TLB

`copy_page_table_level`（[fork.cpp](../../kernel/proc/fork.cpp) L49-102，fork 与 clone 共用）把**父进程自己的在用 PTE** 从 writable 改写成 read-only+`FLAG_COW`（L97-98 `src_table[i].raw &= ~FLAG_WRITABLE; |= FLAG_COW`），但**全程不刷 TLB**。父 CPU 仍缓存旧的 WRITABLE TLB 项 → fork 返回用户态后父进程下次写该页 **TLB 命中（writable）→ 不 #PF → 直接写到已共享的物理页**，污染子进程稍后的 CoW 副本。

- **单 CPU 稳**：下一次 `context_switch` 因 `prev->addr_space != next->addr_space` 重载 cr3（[scheduler.cpp](../../kernel/proc/scheduler.cpp) `switch_addr_space` → `AddressSpace::activate` = `write_cr3`），父 TLB 被刷，再写时正确 CoW。
- **-smp 2 炸**：父进程在自己 CPU 上持续运行，地址空间不变，cr3 不重载，陈旧 writable TLB 一直在 → 写穿透。这正是 note 里「单 CPU 稳、SMP 偶发」的机制。
- fork 与 clone(无 CLONE_VM) **都**调 `copy_page_table_level`，都有此 bug；note 里「musl clone 稳 6/6」是采样假象 + CLONE_VM 线程路径（根本不调 CoW 拷贝）。

**6-agent workflow 对抗验证确认**：根因 = TLB 一致性（非 mapcount 原子性——mapcount 已是 `__atomic ACQ_REL`）；`handle_cow_fault` 对 fork 路径正确（copy-before-dec + 各自 PTE），无需改。

### 修

fork() 页表遍历后 + clone() `cow_clone_address_space()` 遍历后各加一次 `flush_tlb_all()`（cr3 重载，O(1)，刷掉父 CPU 陈旧 writable 项）：

- [fork.cpp](../../kernel/proc/fork.cpp) L279（PML4 遍历 `copy_page_table_level` 之后、VMA 克隆之前）。
- [clone.cpp](../../kernel/proc/clone.cpp) L115（同上）。

**局部刷足够**：fork 在 IF=0 下跑（SFMASK 清 IF，dispatch 前 no `sti`），父不可被抢占/迁移，cr3 仍在该 CPU；子的 cr3 全新从未加载（首跑 clean TLB）。跨核 TLB shootdown（CLONE_VM 线程才需要）是单独 follow-up，fork 路径不需要。

## 根因 2（pre-existing，已修）：syscall_entry sysretq 不恢复用户 RBP

复现器跑起来先撞到一个**独立的、pre-existing、非 SMP** 的 bug：syscall.S 的退出路径恢复了用户 RBX（frame+80）却**漏了 RBP**（frame+88，入口 L64 `pushq %rbp` 存了）。RBP 是 SysV ABI callee-saved，内核必须跨 syscall 保留。任何用帧指针跨 syscall 的用户代码（musl 编译产物、手写 sys_fork）返回后 RBP 是垃圾 → 首个 `[rbp+off]` 访问 segfault。

shell / hello.c 没炸是因为它们的 syscall wrapper 经 gcc 优化后不跨 syscall 用 rbp；F10 musl 移植迟早会踩到。

### 修

[syscall.S](../../kernel/arch/x86_64/syscall.S) 退出路径，RBX 恢复后加 `movq 88(%rsp), %rbp`（恢复用户 RBP）。trap frame 早已存了，退出只是漏读。

## 复现器：tools/musl/forktest.c

musl 静态用户程序，**裸 SYS_fork(57)** 循环（= shell launch_program 的精确路径）。每轮：fork → 父立即写共享 CoW 全局 `g_marker` → 子读 `g_marker`。CoW 正常则子见 pre-fork 值（clean）；父写穿透陈旧 TLB 则子见父的写（race）。`FORKTEST iters=N races=M clean=K` 走串口。

装成 `/hello` 让 F10-M1 ring-3 smoke（`CINUX_MUSL_HELLO_SMOKE=ON`）在 -smp 2 下启动它。smoke worker 是无 AS 内核线程，fork 不走 CoW；它 fork 出的子 execve /hello=forktest 后，**forktest 自己是 user task，它的 fork 才走 CoW**。

构建/运行（KVM 才有真 TLB/SMP）：
```
tools/musl/build-hello.sh 风格 gcc … tools/musl/forktest.c -o build/musl/forktest
scripts/create_ext2_disk.sh build/ext2.img build/user/shell build/musl/forktest   # forktest 当 /hello
# 跑 -smp 2（QEMU 命令同 run-kernel-test-smp，ext2 换成上面那个）
```
`-DFORKTEST_ITERS=N` 调迭代数；`-DFORKTEST_NO_PARENT_WRITE` 关父 post-fork 写（诊断用）。

## 实证（before/after，ITERS=1 = shell 单 fork 路径）

| 树 | ITERS=1, -smp 2, 3 runs |
|----|-------------------------|
| **未修 CoW**（fork/clone 无 flush，rbp 修保留） | run1 `races=1`（抓到写穿透），run2/3 clean（偶发，合 note「偶发」） |
| **全修**（flush + rbp） | 3/3 `races=0 clean=1` PASS |

→ CoW flush 修复**有效且必要**；单 fork（shell 启动 /hello 的路径）-smp 2 下 CoW 隔离正确。

## 残留（未修，登记 follow-up）

**fork#2 rbp 腐蚀**：`ITERS>=2` 时父在第 2 次 fork 的 sys_fork 返回处 segfault（rbp=0x13）。诊断：rbp 在用户态被腐蚀（frame+88 入口即 0x13），非信号（崩前无 `[SIGNAL]`）、非 PF handler（interrupts.S 全 GPR 存/恢复）、非 syscall 恢复（rbp 修在）。触发与「CoW fault + reap 后再次 fork」相关；**不影响单 fork**（shell 正常启动 /hello 不触发）。需后续单独排查（疑 wait4/yield 路径或某 CoW 后状态）。

## 验证

- 默认 `run-kernel-test` **954/0**；`run-kernel-test-smp`（-smp 2）**954/0**（全修后无回归）。
- forktest ITERS=1 -smp 2 全修 3/3 races=0；单 CPU 同。
- 「单 fork = shell 启动路径」已稳；`make run` GUI shell 敲 /hello 是最终实跑确认（headless 复现器已证 CoW 隔离）。

## commit

- `fix(arch): syscall_entry sysretq 恢复用户 RBP`（syscall.S）
- `fix(smp): fork/clone CoW 改写父页表后刷 TLB`（fork.cpp + clone.cpp + 清 syscall.cpp 诊断打印）+ 复现器 + 本文

## ⚠️ 更正（2026-06-27 同日晚）—— 上面「单 fork = shell 路径已稳」的结论下早了

GUI shell（`make run`，-smp 2）**修完后仍然崩**。三份修后日志（log.txt/log2.txt/log3.txt，时间戳 09:22，晚于 09:14 的修复 commit，且无已删的 `[FORK] dispatch` 打印；production big_kernel 验过含两个修复）：

- **log.txt**：shell fork 子(tid=7) → 子 CoW 一次 → 子进 syscall → `syscall_dispatch` 末尾 `ret`（RIP 0xFFFFFFFF81004D67）**#GP**，弹出的返回地址是垃圾、saved rbp=0 → **子内核栈 pt_regs 帧被砸烂**。
- **log2.txt**：子调 `sys_execve` 时 **path=0x0（NULL）** → EINVAL → shell 重 fork → fork 炸弹（旧症状新诱因）。串口字节交错证明**子在 fork() 返回前就在 AP 上跑**（`Scheduler::add_task`→`wake_idle_ap`）。
- **log3.txt**：`handle_pf` 的 klog 路径**嵌套 #PF**（诊断自己又 fault）→ panic；栈里出现 ASCII（"rne-mode"）+ 0xf00f00f0 填充 = 内存被字符串/缓冲写花。

headless 复现器（forktest）**ITERS=1（单 fork）确实 races=0**（CoW 写穿透那条修对了，实证有效），但 **ITERS≥2 崩**（父 rbp=0x13 在 sys_fork 返回处）。加内核 canary（frame+88 入口/出口对比）**未触发** → **内核 pt_regs 帧没被改写**。结论：崩溃是**用户态控制流被劫持**——父用户栈里的返回地址被写花，跳到 0x401182（sys_fork 中段，绕过 prologue），rbp 是垃圾 0x13。shell 启动期已 demand-page 一堆页，它的 fork 实质就是 forktest 的「第 2 次」fork（AS 已被 CoW 动过），所以一上来就踩。

**根因尚未钉死**。已排除：IST=0 栈别名（#PF IST=0，kernel→kernel 同特权用当前 RSP，不撞 kernel_stack_top）；内核 pt_regs 帧被改写（canary 证伪）；sys_yield 破坏 callee 寄存器（calleetest 2000/0）。候选：① `handle_cow_fault` 跨核释放 old_phys（代码自承认注释 process_new.cpp:115-117，另一核可能读到已释放+复用的页 → CoW 拷贝带垃圾 → 用户栈花）—— 解释 -smp 2 shell 崩，但不解释单 CPU forktest ITERS≥2；② 单 CPU 那个另有原因（疑 wait4/CoW 后某状态）。6-agent workflow 主结论（IST 别名）经核 **不成立**（x86 语义），其副发现（子在 fork 返回前跑、CoW 释放竞态）仍有效。

**净状态**：CoW 写穿透 + syscall RBP 两条**修对了、必要**（forktest races=0 实证），但**不够**——shell 还崩。需后续：钉死用户栈腐蚀源（候选 CoW 跨核释放；可先试「handle_cow_fault 不释放 old_phyS，接受泄漏」消除 UAF 看 shell 是否稳）。canary 已撤（诊断用，非修复）。

