# 2026-06-29 F10 fork child-setup 重写(Linux 风格)— 治 gcc-13 Release+ubsan smoke #GP

## 背景

CI 的 6 配置里只有 **(Release, ubsan)** 一格红:-smp2 ring-3 smoke 在首个
`/hello` fork 子创建处 `#GP`(vector 13)。本机 gcc-16 复现不了(编译器版本偏差是
关键——CI Ubuntu gcc-13.3.0 vs 本机 Arch gcc-16.1.1)。docker `ubuntu:24.04`
(同 CI gcc-13.3.0)+ 本机 musl sysroot seed 复现 + 加桩解码。

接手的"锁定根因"是 *fork 的 `current_rbp` 捕获在 gcc-13 帧布局下算错*。本批做的
第一件事就是**证伪它**(见下"根因"),找到真因并修。

## 根因(两次推翻后钉死,非猜)

加桩 `[GP-DIAG]` 在 `fork()` path-B / `launch_user_program` 打 live 寄存器 + 子
ctx,得到铁证:

1. **fork() 在 gcc-13 下有标准帧指针**(`push %rbp; mov %rsp,%rbp; sub $0x68,%rsp`),
   `__builtin_frame_address(0)` == 裸 `movq %rbp`(match=1),rbp 捕获**没错**。
   → 推翻"current_rbp 捕获错位"。
2. #GP 的 RIP `0x...8111b982` 落在 `.text/.data/.bss` **之外**(跳到垃圾地址),
   backtrace 帧也全是垃圾 → 栈被写花。
3. 决定性证据:`launch_user_program` 入口 `argv=0xFFFF80000B4ACF90`(父栈区!),
   `saved_caller_rbp=[rbp]=0xFFFF80000B4B6FF0`(子栈,正确),`child->ctx.rbp` 也是
   子栈值。**musl 的 rbp 是对的,但 argv 指向父栈**。
4. 反汇编 `musl_hello_smoke_entry`(gcc-13):函数入口 `lea -0x60(%rbp),%rax` 把
   `argv_base = rbp-0x60` **存进了自己的栈帧槽**,跨 `fork()` 调用保留;子分支再
   从该槽 load 出来当 argv。gcc-16 是内联重算,所以只 gcc-13 炸。

**真因**:子进程 `memcpy` 继承了父栈拷贝里那份**未重定位**的 `argv_base`(父栈地址),
用它寻址 `argv[]` → argv 落父栈 → 被覆写 → `build_initial_stack` 的 `str_len(垃圾
argv[i])` 解引用非规范地址 → `#GP`(x86 非规范地址访问报 #GP error=0,不是 #PF)。

旧的 `prepare_copied_kernel_stack_context` 的 RBP 链逐帧重定位**只修了 saved-rbp 链**,
没修 `argv_base` 这类普通 spill——治不到。

## 改法(Linux 风格 fork child,按调用方有无 `addr_space` 分两条路径)

### A. 用户态 fork/clone(`addr_space != nullptr`)— shell 命令 fork / pthread

子拿**干净内核栈**,只在栈顶拷父的 128B syscall pt_regs 帧(从
`kernel_stack_top-128`,clone.cpp 早证明此偏移可达);`ctx.rip = ret_from_fork`。
新增 `ret_from_fork`(syscall.S)镜像 `syscall_entry` 返回尾:从帧读
user RIP/RSP/RFLAGS + callee-saved,`rax=0`,`clac/cli/swapgs/sysretq` 回 Ring 3。
**不拷父栈、不走 RBP 链**,彻底免疫编译器帧布局差异。

### B. 内核态 fork/clone(`addr_space == nullptr`)— shell 启动 / ring-3 smoke

仍拷父在用栈段(保留调用方帧),`ctx.rip = fork_child_trampoline`(`xor rax,rax;ret`),
帧基用 `__builtin_frame_address(0)`(编译器跟踪,比裸 `movq %rbp` 稳)。**删 RBP 链
逐帧重定位循环,改全扫**:遍历整个拷贝区,把每个指向父(已拷)栈的 qword 重定位
(saved-rbp / argv_base / 任何 spill 一网打尽;返回地址/小整数/外指针不在区间内不
动)。这是治本点。

## 涉及文件(commit dd1a78e)

- `kernel/arch/x86_64/syscall.S` — 新增 `ret_from_fork`(**手写,不走 clang-format**
  ——本机 clang-format v22 会把 asm 注释毁成 `#== == ==` 垃圾;CI format job 已因
  v22≠v18 禁用,靠手写纪律)。
- `kernel/proc/process.hpp` — 声明 `ret_from_fork`。
- `kernel/proc/process_internal.hpp` — 共享 `kSyscallFrameSize=128` +
  `prepare_user_fork_context` + `prepare_kernel_fork_context`(替原
  `prepare_copied_kernel_stack_context`)。
- `kernel/proc/fork.cpp` / `clone.cpp` — A/B 分流;**删 per-function
  `optimize("no-omit-frame-pointer")` 属性**(全局 `-fno-omit-frame-pointer` 已覆盖,
  per-function 属性在 gcc-13 下有选项重置隐患——曾是最初的怀疑方向);B 路径全扫重定位。
- `kernel/test/test_fork_exec.cpp` / `test_clone.cpp` — `copied_rbp_chain_is_relocated`
  → `child_resume_context_is_valid`(A: ret_from_fork + 帧在栈顶;B: trampoline +
  rbp 在栈内)。

## 验证

docker `ubuntu:24.04`(gcc 13.3.0-6ubuntu2~24.04.1,同 CI)+ 本机 musl sysroot seed:

- **Release+ubsan** `run-kernel-test-all` 两 leg(单核 + -smp2)全绿,smoke hello
  **20/20 PASS**,零 #GP/#DF/PANIC。← 原 #GP 配置转绿(6/6 应全绿,push 后 CI 验)。
- 本机 **gcc-16** 两 leg 全绿 + smoke 20/20(无回归)。
- 源码净(TEMP GP-DIAG 桩全撤,`grep GP-DIAG` = 0)。

## 关键决策 / 教训

- **"锁定根因"也要复核**:接手的 `current_rbp` 诊断两次被证伪(rbp 其实没错)。加桩
  打 live 寄存器拿到 `argv` 落父栈的铁证,才找到真因(栈帧里**未重定位的 spill 指针**)。
  对齐 `dont-ask-whether-to-investigate`:追根因/加仪器默认就做。
- **拷贝父栈 + 中途恢复 = 脆**:编译器把任意栈指针 spill 进帧(saved-rbp 只是其中一
  类),逐链重定位治不全。全扫重定位是治本(启发式:扫到落在父栈区间的 qword 就重定位;
  非栈值不动,误伤极少)。Linux 对 user fork 用干净栈 + ret_from_fork 正是回避这套。
- **本机 gcc-16 ≠ CI gcc-13**:smoke 必须用 docker gcc-13 验(本机复现不了=白验)。
  docker 配方:`ubuntu:24.04`(原生 gcc-13,免 PPA)+ `apt-get -o Acquire::ForceIPv4=true`
  + 禁非 ubuntu 源;musl-sysroot 用本机 seed 免下载 stall。
- **坑**:build dir 名 ≠ `build` 时,`build-hello.sh` 默认输出到 `build/musl/hello`,
  而 `CMAKE_BINARY_DIR/musl/hello` 找不到 → ext2 镜像漏 `/hello` → smoke 假性
  "inode not found"(掩盖真 #GP)。docker 用 `build-gcc13` 时要把 hello/forktest 也
  编到 `build-gcc13/musl/`。
- 路径 A(user fork)的 `ret_from_fork` 执行路径**未被 CI smoke 覆盖**(smoke 走 path B;
  path A 是 shell 命令 fork,仅 GUI `make run` 验)。靠镜像 `syscall_entry` 返回尾 +
  gcc-16 两 leg 构造性正确。push 后用户可 `make run` 抽验 shell fork。

## 残留 follow-up

- 路径 A(`ret_from_fork`)执行仅在 GUI shell 验;CI 无 user-fork 真执行测试。
- 全扫重定位是启发式(理论上非指针 qword 落在栈区间会被误重定位,实践中罕见)。
- 这些是既有覆盖缺口,非本批回归。
