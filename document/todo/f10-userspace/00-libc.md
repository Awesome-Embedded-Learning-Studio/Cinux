# M1: musl 静态移植（F10 重构，2026-06-26）

> **方向决策（用户 2026-06-26）**：不自建 libc 生态。`user/libc`（syscall/string/printf 三件套）
> 仅作 QEMU 测试壳保留，**不扩**。**直接移植 musl 作唯一 libc**：先自己源码编译 musl（自包含
> sysroot），跑通后切 **musl-gcc** 当工具链驱动，上层 userland 一律 musl-gcc 编。
>
> 本文取代旧版「自建 libc 扩到 80 syscall」规划（造轮子无意义）。旧 M5「musl+glibc」内容并入本 M1；
> glibc 静态兼容降为可选 stretch（比 musl 重得多，musl 先）。
>
> 真实进度源：`document/ai/PLAN.md`「🔄 F10-M1」批表。本文件是 M1 的 ABI 差距清单 + syscall 待补表。

## 目标

musl `-static` 编译的 hello world 经 execve + ELF loader 在 Cinux QEMU 上跑起来，`printf` 真输出。
即：内核 syscall ABI 对齐 Linux x86_64，足以让 musl 的 `__init_libc` / `__libc_start_main` 启动 +
基础程序运行。

## 调研实证（决定打法，已读码核实）

- **返回约定基本已是 Linux 风格**：`sys_open` 返 `-to_errno(...)`（`kernel/syscall/sys_open.cpp:42`，
  `kernel/errno.hpp::to_errno`）= musl 要的"负 errno"。仅几个老 syscall 返裸 `-1`：
  `sys_getpid.cpp:20` / `sys_getppid.cpp:17` / `sys_pipe.cpp:56,86,98`。→ 批1 收尾改 `-errno`。
- **execve 未压 auxv**（grep 全空）—— **musl 头号拦路虎**。musl `__init_libc` 启动必从栈读
  `AT_PHDR/AT_PHNUM/AT_PAGESZ/AT_RANDOM/AT_ENTRY...`。→ 批3 补（向后兼容：老程序读不到无害）。
- **`SYS_chdir=12` 与 `SYS_brk=12` 撞号**（`syscall_nums.hpp:28,48`），brk 后注册覆盖 chdir →
  **chdir 当前是坏的**。Linux 正确号 chdir=80、brk=12。→ 批1 全表对齐 Linux x86_64 号顺手修。
- **musl 走 `*at` 家族**（`openat=257` / `newfstatat=262`），不调老 `open/stat`。
  → "补 syscall" = 补 musl 真正会发的那 ~25 个，非旧规划的 80。
- 现有 ELF 加载：`kernel/proc/execve.cpp` + `kernel/mini/elf_loader.cpp` 在用（加载测试 shell）；
  musl 静态 ELF 走同路径，唯一缺口是 auxv。

## 批分解（每批≈一 commit，门 `timeout 40 run-kernel-test` 绿）

| 批 | 范围 | 完成门 |
|----|------|--------|
| 0 | 立项 docs（PLAN/ROADMAP/本文件 + 清 PLAN 顶 3 段过期状态）| docs-only |
| 1 | syscall 号纠偏（chdir 12→80，全表对齐 Linux x86_64）+ 返回约定收尾（getpid/getppid/pipe `-1`→`-errno`）| run-kernel-test 绿 + chdir 回归 |
| 2 | Linux 结构体布局：`sys_stat`==Linux stat(144B) / `UserSigAction`==Linux sigaction / sigset / iovec 对齐 | run-kernel-test 绿 + stat/signal 回归 |
| 3 | execve 压 auxv（AT_PHDR/PHNUM/PAGESZ/RANDOM/ENTRY…），向后兼容 | run-kernel-test 绿 + 现有 shell 不崩 |
| 4 | 补 musl 所需 syscall（见下表；装不下拆 4a/4b）| run-kernel-test 绿 + 每个新 syscall 最小测 |
| 5 | musl 源码编译 + sysroot（configure+make → libc.a/crt1.o），自包含；`-static` 编 hello world | host 产出 musl 静态 ELF |
| 6 | 端到端：musl hello world 在 QEMU 跑通，printf 输出；加测试项；notes | run-kernel-test 绿 + hello world 真输出 |

## 待补 syscall（批4，按 musl 启动 + 基础程序所需；对照 Linux x86_64 号）

> 原则：只补 musl 真会发的，不堆全 80。已实现的（read/write/open/close/exit/mmap/munmap/
> mprotect/brk/futex/rt_sig*/clone/execve/waitpid/getpid/getuid…/setpgid 等）核对语义即可；
> 下列是 musl 会调但 Cinux **缺失**或**号错**的。

| # | Syscall | 状态 | 备注 |
|---|---------|------|------|
| 257 | openat | 缺失 | musl 的 open/fopen 走它（非 old open=2）|
| 262 | newfstatat | 缺失 | musl 的 stat/fstat 走它（非 old stat=4/fstat=5）|
| 231 | exit_group | 缺失 | musl exit() 用 exit_group，非 exit=60 |
| 61→247 | wait4 | 缺失 | musl waitpid 走 wait4（Cinux 现有 waitpid=61 需核对/补 wait4）|
| 8 | lseek | 缺失 | |
| 9/11/10 | mmap/munmap/mprotect | 已有 | 核对语义（musl 期望 -errno + 布局）|
| 158 | arch_prctl | 缺失? | musl 设 TLS（fs_base）；Cinux clone 已设，核对 |
| 14/13/15 | rt_sigprocmask/action/return | 已有 | 核对 sigset 布局（批2）|
| 13/202 | rt_sigaction/futex | 已有 | |
| 102-108 | getuid/gid/euid/egid/setuid/setgid | 已有 | F9 |
| 96/99 | gettimeofday/clock_gettime | 缺失 | musl 时间（PIT/HPET 源）|
| 12 | brk | 已有(号对) | brk=12 正确，修的是 chdir 撞号 |
| 80 | chdir | 号错(现=12) | 批1 改 80 |

> 批4 边做边对照 musl 源码（`src/internal/` / `arch/x86_64/syscall_arch.h`）确认真正调用集，
> 不靠拍脑袋。每补一个加最小 run-kernel-test 测试项。

## 风险

- **批3 auxv**：碰程序启动核心路径，保现有 shell 不崩（auxv 追加在 envp 后，老程序读不到无害）。
- **批5 musl 源码编译**：host 工具链依赖，最大外部不确定项。musl 用 `./configure` + `make`，
  需一个能产出 Cinux 目标 ELF 的 GCC（现 GCC 14 + 自定义 link 脚本/sysroot）。
- **批1 改号**：ABI 破坏性，碰 `user/libc/syscall.h` + shell，全量回归。
- **批2 结构体**：`stat`/`sigaction` 若与 Linux 布局不一致，musl 静态二进制读错字段——对着
  glibc `<bits/struct_stat.h>` / Linux UAPI 逐字段核。

## 产出物

- [ ] Linux ABI 兼容的 syscall 面（号 + 返回约定 + 结构体）
- [ ] execve auxv 辅助向量
- [ ] 自包含 musl sysroot（libc.a / crt1.o / crtbegin/end）
- [ ] musl `-static` hello world 在 Cinux QEMU 跑通
- [ ] M1 完成后演进：用户程序工具链驱动切 **musl-gcc**

## 后续里程碑（M2-M5 不变，待 M1 完成排期）

- M2 ELF 动态链接（+ musl 动态 / ld-musl）— [01-elf-dynamic.md](01-elf-dynamic.md)
- M3 TTY 子系统 — [02-tty.md](02-tty.md)
- M4 PTY 真终端会话(busybox sh 作消费者)
- glibc 静态兼容验证（可选 stretch）— [04-musl-glibc.md](04-musl-glibc.md)
