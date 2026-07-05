# 2026-07-05 GCC 自举 + 可用性线 — 阶段收尾

## 结论

CinuxOS 上 **gcc/g++ 编译 + 跑用户程序**全链路绿,CI 全绿。从 2026-07-02 立项到 2026-07-05,4 天 / 6 PR,F12-M2(GCC 自举)+ F-USABILITY(可用性)核心目标达成,标记**阶段收尾**。下一步是 F12-M3 self-hosting(编 CinuxOS 自己)或别的 milestone,本线 follow-up 挂起。

## 达成清单(PR 链)

| PR   | 内容                       | 关键                                                                                                |
|------|----------------------------|-----------------------------------------------------------------------------------------------------|
| #62  | b4-gcc-toolchain           | cc1/as/ld + glibc 闭包装盘,as+ld 自举链路                                                          |
| #63  | f-usability                | Buildroot rootfs + busybox init PID1 + isa-debug-exit usability gate                                |
| #64  | f-usability-b4-gpp 批4a    | g++ 单命令闭环 + 7 bug 链                                                                           |
| #65  | f-usability-b4-gpp         | g++ enabled + C 重构治 lto_plugin corrupt(refcount/pte_count 分离)                                |
| #66  | perf-b1-stats-profiling    | perf 弧(ext2 agg −25% I/O)+ 默认 PIE gcc/g++ + syscall 补全(unhandled 90→0)                     |

## 关键里程碑

1. **F12-M2 批4-C2**(2026-07-02):cc1→as→ld→./hello 全自举闭环(CinuxOS 自己编自己跑的 C 程序)。4 bug:stdc-predef.h 隐式 preinclude / O_TRUNC 实现 / user #PF→SIGSEGV(内核兜底)/ VMA backing inode refcount(slab 复用 UAF 根因)。
2. **F-USABILITY 批3**(2026-07-03):gcc driver 单命令 `gcc hello.c -o /tmp/a.out && /tmp/a.out`。5 根因:CLONE_VFORK parent block / forced SIGSEGV / VMA backing-offset / tmpfs O_CREAT mode / waitpid reaped_pid。
3. **批4a g++**(2026-07-04):g++ hello.cpp(STL vector/string + 异常 throw/catch)。7 bug 链(do_openat follow symlink 头号 / waitpid status 编码 / kprintf %.\*s / extract libm self-loop / ProcFs+DevFs lookup_child / assemble 192MB / bb 断言解码)。
4. **C 重构批2-4**(PR#65):lto_plugin corrupt 真根治。refcount/pte_count 语义分离(组合语义 `pte_count_dec_and_test` 替代无条件双 dec,fork CoW bug)。治 sys_munmap 绕过 mapcount → double-free cache phys → PMM 复用 → lto_plugin invalid ELF。
5. **perf 弧 B1→B3a**(PR#66):gcc 编译卡顿定位(72% ext2 I/O)。elf_load lazy demand paging(cc1 加载 22s→0s)+ ext2 read 聚合连续 block(I/O −25%)+ SIGILL 修复(read 越界 straddle BSS)。
6. **PIE B1+B2**(PR#66):默认 PIE gcc/g++(完整 main-base ASLR)。execve ET_DYN 检测 + USER_EXEC_BASE(0x20000000)+ aslr_exec_base_offset。⭐踩坑:brk heap USER_BRK_MAX 绝对上限被 PIE base(512MB)撞穿 → brk_max 跟随 image。⭐踩坑:GUI=ON 让 usability gate 卡(kernel_init 走 GUI 不跑 busybox init)。
7. **syscall 补全**(PR#66):gcc/g++ 编译 unhandled 90→0。8 个(prlimit64/getrandom/rseq/set_robust_list/clone3/gettimeofday/time/sendfile)。⭐`kernel/syscall/CMakeLists.txt` 手动列源,加 .cpp 必须改它。

## 验证基线

- `run-kernel-test-all` 两 leg:hello(ET_EXEC)20/20 + hello-dyn(PIE)5/5。
- `run-buildroot-usability` gate:gcc-compile-run + gpp-compile-run PASS(默认 PIE)。
- unhandled syscall **90 → 0**。
- CI 全绿(PR#66 merged)。

## open follow-up(非 gcc/g++ 编译必需,登记留后续)

- **pthread**(F-USABILITY 批4b ⏳):gettid/sched_getaffinity + clone(CLONE_THREAD)真跑。
- **robust futex 真清理**:`set_robust_list` 现返 0 假成功(pthread 死清 robust lock 未实现)。
- **sendfile 真实现**:cp/copy 大文件优化(现 fallback read+write,功能正常)。
- **199 fremovexattr + xattr 系列**:test 路径 2 次。
- **interpreter ASLR**:ldso 固定 USER_INTERP_BASE(与 main base 配对的 follow-up)。
- **静态 PIE**:entry_va 路径覆盖未实证(需 musl 静态 PIE smoke)。
- **ext4 rootfs**:F6-M5(extent 写)。
- **F12-M3 self-hosting**:在 CinuxOS 编 CinuxOS(远期大弧,要更多 syscall + 工具链稳定)。

## 结语

GCC 线核心目标「CinuxOS 能用 gcc/g++ 编译并跑用户程序」达成。memory:[[gcc-selfhost-handoff]] / [[gcc-self-host-strategy]] / [[f-usability-b4a-gpp]] / [[gcc-compile-stutter-perf]] / [[pie-support-handoff]] / [[gcc-missing-syscalls]]。
