# 交接 prompt:继续 A 重写(fork child-setup Linux 风格 ret_from_fork)

> 把下面这段作为新会话的开场白粘贴即可无缝接手。memory 已顶置(MEMORY.md「⭐下轮最优先·A 重写」),plan 在 `~/.claude/plans/parallel-wiggling-corbato.md`。

## 粘贴用 prompt

```
继续 CinuxOS 的 fork child-setup A 重写(治 CI Release+ubsan gcc-13 #GP)。

背景(都已查清,别重查):CI (Release,ubsan,gcc-13.3.0) 的 -smp2 ring-3 smoke 在
首个 /hello fork 子处 #GP(vector 13)。根因锁定:fork child-setup 的
prepare_copied_kernel_stack_context 用 ctx.rsp=relocate(current_rbp+8) 这套 current_rbp
捕获,在 gcc-13 -O2+ubsan 帧布局下算错 → child 跑错栈 → argv 落栈外被覆 →
build_initial_stack 的 str_len(垃圾 argv[i]) #GP。已两次排除:①不是 RBP 链重定位循环
(B commit 73b4b5e 跳过它、gcc-13 仍 #GP,且打破 copied_rbp_chain_is_relocated 单测→全红,
已 revert 92b3ecf);②不是栈溢出(gcc-13+ubsan 下 musl_hello_smoke_entry 帧<1024B,无
-Wframe-larger-than)。详见 memory f10-shell-launch-smp-fork-race + plan
~/.claude/plans/parallel-wiggling-corbato.md。

任务 = plan 的 A:fork/clone child-setup 重写成 Linux 风格 ret_from_fork——child 拿干净
内核栈(用户 fork:栈顶放父 syscall 帧 128B @ kernel_stack_top-128,clone.cpp 已证此偏移
可访问),ctx.rip=新 ret_from_fork(读帧用户寄存器 + rax=0 + swapgs;sysretq 回用户态),
彻底不拷父内核栈/不捕获 current_rbp/不走 RBP 链。kernel-fork(smoke,addr_space==nullptr)
用 __builtin_return_address 捕获+直接恢复(不依赖帧布局)。同步改 clone.cpp + 处理
test_fork_exec::copied_rbp_chain_is_relocated 单测(机制变了,断言要改/删)。涉及文件:
kernel/arch/x86_64/syscall.S(新 ret_from_fork)、kernel/proc/fork.cpp、clone.cpp、
process_internal.hpp。

当前态:分支 fix/smap_bug_fix,B 已 revert(92b3ecf),CI 5/6(Release+ubsan=本 #GP deferred)。
本地 build/ 已 CINUX_NO_KVM=1 重配(KVM 权限坏,TCG 慢但能跑)。

验证纪律(别重蹈 B 覆辙):每步 docker gcc-13 验(本机 gcc-16≠gcc-13 不能代表)。docker 配方:
  docker run --rm --pull=never -v $PWD:/work -w /work catthehacker/ubuntu:act-latest bash -c '
    rm -f /etc/apt/sources.list.d/*.list                       # 禁非 ubuntu 源(microsoft/launchpad/packagecloud stall)
    apt-get -o Acquire::ForceIPv4=true -o Acquire::http::Timeout=15 -o Acquire::Retries=1 update -qq
    apt-get -o Acquire::ForceIPv4=true install -y -qq cmake qemu-system-x86 e2fsprogs xxd
    export MUSL_SYSROOT=/work/build/musl-sysroot               # 本地 seed,免 musl 下载
    tools/musl/build-musl.sh; mkdir -p /tmp/kb/musl
    tools/musl/build-hello.sh /tmp/kb/musl/hello; tools/musl/build-forktest.sh /tmp/kb/musl/forktest
    cmake -B /tmp/kb -DCMAKE_BUILD_TYPE=Release -DCINUX_BUILD_TESTS=ON -DCINUX_UBSAN=ON -S /work
    cmake --build /tmp/kb --target run-kernel-test -j$(nproc)   # gcc-13 Release+ubsan 复现 #GP
  '
本地全绿(单核+-smp2 两 leg + test_host)+ docker gcc-13 全绿 再 push(push 归用户)。
看全:别只盯 smoke hello,单测结果(copied_rbp_chain_is_relocated 等)+ 多配置都要绿。

先读 plan + memory,然后开干 A(建议从 ret_from_fork trampoline + user-fork 路径开始)。
```
