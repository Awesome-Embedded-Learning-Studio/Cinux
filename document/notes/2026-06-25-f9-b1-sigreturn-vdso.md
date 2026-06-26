# F9 批1:sigreturn trampoline vDSO 化(脱离栈上可执行代码)

> 2026-06-25 · F9 安全机制 · 批1 · `feat/f9-security` · commit `4a16158`
> 前置:为批2 开 EFER.NXE + 用户栈标 NX 扫清头号障碍。

## 背景

F9 开 NXE 的拦路虎:`signal_setup_frame`([signal.cpp](../../kernel/proc/signal.cpp))把 8 字节 `int $0x80` sigreturn trampoline **写在用户栈上**,handler `ret` 落此执行进 sigreturn gate(vector 0x80)。注释明说「依赖用户栈可执行(NXE off until F9)」。一旦开 NXE + 栈标 NX,这段栈上代码执行不了 → sigreturn 崩。

批1 在开 NXE 前,先把 sigreturn 从栈上可执行代码里解放出来——挪到 execve 映射的固定用户可执行页,对齐 Linux vDSO `__restore_rt`。**行为不变**(信号 round-trip 照常),纯重构。

## 改动

- **[usermode.hpp](../../kernel/arch/x86_64/usermode.hpp)**:加 `USER_SIGRETURN_PAGE = 0x100000`(ELF base 0x400000 之前的空旷低地址,经典 vDSO 位)。
- **[signal.hpp](../../kernel/proc/signal.hpp)**:把 `kSigreturnTrampoline[8]`(`int $0x80` + nops)从 signal.cpp 匿名 namespace 提到头文件,供 execve 写页。
- **[signal.cpp](../../kernel/proc/signal.cpp)**:`signal_setup_frame` 去掉栈上 T 槽(8B trampoline),返回地址槽直接填 `USER_SIGRETURN_PAGE`;布局 `R = user_rsp - pad - sizeof(SignalFrame)`(少减 8)。删匿名 `kSigreturnTrampoline`。
- **[execve.cpp](../../kernel/proc/execve.cpp)**:ELF 加载 + heap VMA 后、return Ok 前,映射 sigreturn 页——alloc 物理 + 清零 + 写 8B trampoline + `map(USER_SIGRETURN_PAGE, phys, PRESENT|USER)`(只读、可执行 = 不设 NX)。

## 关键点

- **RSP 语义不变**:handler `ret` 弹出返回地址后 RSP = R+8 正好指向 SignalFrame(`sigreturn_handler` 读 `frame->rsp`)。新机制只改返回地址的值(从栈上 T 变固定页),RSP 布局不动 → `test_sigreturn_restores_context` 无需改仍通过。
- **fork/clone 无需重映射**:CLONE_VM 共享地址空间(含 sigreturn 页);execve 是唯一创建新地址空间的路径,在那里映射覆盖所有用户程序。

## GOTCHA

- **测试要跟机制同步**:[test_signal.cpp](../../kernel/test/test_signal.cpp) `test_setup_frame_redirects_to_handler` 原断言读返回地址指向的字节(`tramp[0]==0xCD`),验证栈上 trampoline。批1 挪走后该断言失效(返回地址变 0x100000,测试栈缓冲区没映射那),首跑 **930/1 失败**。改为验 `ret_addr == USER_SIGRETURN_PAGE`。教训:改机制(尤其地址/布局)要 grep 测试同步更新。

## 验证

- 全量编译(改了 signal.hpp/usermode.hpp 两个头)100% 零错。
- run-kernel-test **931/0**(含 test_sig_frame 信号 round-trip),clang-format 后复跑仍 931/0。

## 下一步

批2:开 EFER.NXE(usermode.S 加 bit 11 / CPUID 检测)+ 补 NX(mmap/demand-read)+ 用户栈标 NX + 清五处 deferred 注释 + execve 核实 + make run 冒烟。
