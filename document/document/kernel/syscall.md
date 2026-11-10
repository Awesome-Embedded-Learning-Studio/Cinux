# 系统调用

## 功能概述

这一页我们把 ring 3 向内核求助的那层编号接口完整地走一遍。用户程序放号码的格子是 rax，排参数的是 rdi 到 r9，做完了这些，一条 syscall 指令就把任务送进了内核，内核按号码查表、把该干的活干完，干完之后的返回值落回 rax。编号的约定、进出的汇编、帧的摆法、分发的挂表、用户指针的搬运、阻塞时的让出、出口顺路的信号交付，全在我们这一页的范围里，实现的住处是 `kernel/syscall/` 与 `kernel/arch/x86_64/syscall.S` 两处。

约定的基座是三条，号码跟 Linux x86_64、寄存器跟 Linux x86_64、错误以负的 errno（错误号，libc 按它设置自己的 errno 变量）回去。照 `syscall.hpp` 文件头注释的说法，未来的 musl 与它背后的整个 rootfs，对上面三条是不问自取的，所以静态链接的 musl 程序按这套号码原样发调用，我们也就不需要替 busybox 与 shell 做任何的适配。

旧页的几处事实已经过期，我们核过当前的源码树，按现在的样子重讲。分发表的 C++ 侧从 `kernel/arch/x86_64/syscall.hpp/cpp` 搬进了 `kernel/syscall/`，架构目录里剩下的只有 `syscall.S` 一份汇编，表也从 256 个函数指针换成了 440 个槽位的编译期数组。入口汇编里已经没有了 swapgs 这一手，用户指针的检查也不是旧页说的 canonical address 校验，换成了上限检查加搬运路径上的缺页兜底。旧页总表里还有几处对不上号的细节：SYS_getdents 的号码是 217 而不是 78，SYS_getpid 行指的 `kernel/proc/pid.cpp` 已经不在源码树里，getpid 的实现落在 `kernel/syscall/sys_getpid.cpp`，SYS_execve 行指的 `sys_fork.cpp` 也该换成 `sys_exec.cpp`。

兄弟页的分工我们在开头摆好。fork、clone、execve 与 wait 的入口细节归 [进程的复制与替换](proc/fork_exec.md)，pid、tgid、进程组与会话的号码语义归 [任务的身份](proc/pid.md)，futex 与锁族的停车归 [同步原语](proc/sync.md)，yield 背后的挑选归 [进程与调度器](proc/process_and_scheduler.md)。这一页只讲系统调用层自己的机制，几族的入口我们各挑代表读，族内的细节都留在各自的页面里。

## 号码、返回值与 errno

号码住在一个叫 `SyscallNr` 的枚举里，底层类型是 16 位的无符号数，全部照 Linux x86_64 的编号写定。我们数几个大家认识的，read、write 与 getpid 的号码是 0、1 与 39，fork、execve、getdents 的位置是 57、59 与 217，openat 的号码是 257，靠后的成员里也有 close_range（436），clone3（435）的名号也在列。表的大小由 `kSyscallCount = 440` 给出，它是一个比全部已用号码都大的数，分发数组开的槽位就照这个数走。

errno 的词汇表也住在同一个头文件里，`kEnosys`、`kEbadf`、`kEagain` 与 `kEintr` 的值分别是 38、9、11 与 4，处理函数的返回值带上负号交出去，ring 3 拿到的就是我们在 libc 里认识的 errno。内核自己的错误是 `KernelError` 枚举，`sys_common.cpp` 里的 `ErrnoFrom` 负责翻译，kNotFound、kWouldBlock、kInterrupted 与 kTooManyOpenFiles 翻译过来的是 ENOENT、EAGAIN、EINTR 与 EMFILE，翻不认识的按 EINVAL 处理。

编号表之外还有一层词汇的工作，open 的 O_* 位、clone 的旗位与 tid 字段，mmap 的 prot 与 MAP_* 位，reboot 的两个口令字与命令值，全都装在这个头文件的里面。clone 的旗位我们得一个个认，共享的旗位只有 kCloneVm 与 kCloneFiles 两个，前一个让孩子跟父亲共用地址空间，后一个共用的是打开文件表，kCloneThread 并的是线程组，kCloneSetTls 立的是线程指针，各自只管各自的那件事。reboot 那边我们把词分开数，口令字写的是 kRebootMagic1 与 kRebootMagic2 两个数，源码注释给它们的叫法是 authorization word，剩下的 kRebootPowerOff 是命令值，我们把它归在另一头。交给 ring 3 的字节布局有两份承诺，stat 给用户的 `SyscallStat` 按 Linux x86_64 的 144 字节布局摆，我们核过的关键偏移有 static_assert（编译期断言）看着，getdents 的目录记录按偏移手摆成 dirent64 的形状，贴着排的记录整长都是八的倍数。

## 进门的底座：EFER、STAR、LSTAR 与 SFMASK

SYSCALL 指令真正通到内核的底座是四件 MSR（模型专用寄存器）的配置，我们把配置的活读一遍。干活的函数是 `kernel/arch/x86_64/usermode.cpp` 里的 EnableFastSystemCalls，引导序列里名叫 fast system calls 的那一步调它，同一步里还挂上了异常表（内核里成对登记出错点与修复口的表），按 CPUID 的回答开了 SMAP（管理员态访问保护）的窗口，引导步的三行在 `kernel/init/init_sequence.cpp` 里。

```cpp
// kernel/arch/x86_64/usermode.cpp
void EnableFastSystemCalls() {
    unsigned long long const kEfer =
        ReadMsr(msr::kEfer) | msr::kEferSyscallEnable | msr::kEferNoExecuteEnable;
    WriteMsr(msr::kEfer, kEfer);
    const unsigned long long kReadBack = ReadMsr(msr::kEfer);
    base::safety::Check(((kReadBack >> 11U) & 1ULL) != 0ULL, "efer refused the no-execute enable");
    WriteMsr(msr::kStar, kStarValue);
    WriteMsr(msr::kLstar, reinterpret_cast<unsigned long long>(&SyscallEntry));
    WriteMsr(msr::kSfmask, kInterruptFlagMask);
}
```

这段十行我们一句句读。EFER 里开的是 SCE 与 NXE 两位，写完了读回来，确认 NXE 真的立住了，立不住的话，Check（这个内核的运行时断言）会当场拦下。STAR 的摆法是两头折叠，内核码段 0x08 放在了 32 到 47 位，SYSRET 的基座 0x23 放进了 48 到 63 位，RPL 3 直接折进基座本身（gdt.hpp 注释的口径），sysretq 按基座加 16 算出 0x33 的用户码段，加 8 算出 0x2B 的用户数据段，两个结果的背后都有 static_assert 守着。LSTAR 装的就是 SyscallEntry 的地址，SYSCALL 执行起来的那一刻，rip 就跳到了那里。中断的开关是这么接的，SFMASK 只装了 IF 一位（第 9 位），SYSCALL 落地的瞬间中断是关着的，入口汇编里的 sti 随后把它们放开了，出口前的 cli 再关上，这个节奏我们在下一节的汇编里对着看。

## 入口汇编 SyscallEntry

进门的第一站是 `kernel/arch/x86_64/syscall.S` 里的 SyscallEntry。旧页写的 swapgs 在现在的汇编里找不到，per_cpu.hpp 的注释把原因交代了：IA32_GS_BASE 从装好的那天起就常驻 per-CPU（每处理器一份）块，任何特权级用的都是它，入口汇编寻址用的就是 gs:0 与 gs:8，前者是内核栈的座位，后者是用户栈的中转，帧指针则存在了 gs:24，我们也就不需要多一手 swapgs。

```asm
# kernel/arch/x86_64/syscall.S
SyscallEntry:
    movq %rsp, %gs:8
    movq %gs:0, %rsp
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbp
    pushq %rbx
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax
    pushq %r11
    pushq %rcx
    pushq %gs:8
    movq %rsp, %gs:24
    call EnterFpuGate
    sti
    movq %rsp, %rdi
    call InvokeSyscall
    cli
    movq %rax, 24(%rsp)
    pushq %rax
    subq $8, %rsp
    call ExitFpuGate
    addq $8, %rsp
    popq %rax
    movq 32(%rsp), %rdi
    movq 40(%rsp), %rsi
    movq 48(%rsp), %rdx
    movq 56(%rsp), %r10
    movq 64(%rsp), %r8
    movq 72(%rsp), %r9
    movq 8(%rsp), %rcx
    movq 16(%rsp), %r11
    movq 80(%rsp), %rbx
    movq 88(%rsp), %rbp
    movq 96(%rsp), %r12
    movq 104(%rsp), %r13
    movq 112(%rsp), %r14
    movq 120(%rsp), %r15
    movq (%rsp), %rsp
    sysretq
```

进出的节奏我们读一遍。进门时 rsp 还是用户的栈，它被存进了 gs:8，随后从 gs:0 装来这颗核的内核栈顶，接着的十六个 pushq 是这样的，rcx（SYSCALL 存下的返回地址）与 r11（存下的 rflags）排在前头，六个 callee-saved 寄存器垫了底，最后压进的是 gs:8 里的用户栈，它落在帧的 +0。rsp 存进了 gs:24 之后，任务在内核里的整套用户寄存器就都有了物理的座位。EnterFpuGate 过一道浮点的门，第一次进门把当前的浮点图像存进任务的 entry_fpu，层数记在了 fpu_gate_depth 上，出门的时候减到零才真的恢复，中断与异常路径走的是同一对门，层数用的是同一个互不覆盖的计数。这对门的实现在 `kernel/proc/fpu_gate.cpp`。sti 放开的是中断，之后的内核体是开着中断跑的，随后的 `movq %rsp, %rdi` 把帧指针当成了第一参数，call 就进到了 InvokeSyscall。

出口是反着来的，我们也照着读一遍。cli 关掉了中断，返回值写回帧的 rax 槽，隔着一次 ExitFpuGate 的调用，rax 被收好又放了回去，因为谁也不担保 C 函数归还时的 rax 还是原值。然后是按帧的逐个恢复，rdi 到 r9、rcx、r11 从帧里领回了自己的值，六个 callee-saved 领回的也一样，最后的 `movq (%rsp), %rsp` 把帧 +0 存的用户栈直接装回 rsp，sysretq 就落回了用户态，rip 取的是 rcx 里的值，rflags 取的是 r11 里的。

这套安排的用意，InvokeSyscall 的注释说了，我们把它的意思记下，处理函数的让出、退出与停车都发生在内核的调用中间，任务被切走又切了回来，帧还躺在它自己的内核栈上，出口的恢复动作接着做就是了。同一个文件的下半段是第二个入口 ForkChildEntry，那是 fork 的孩子醒来落的地方，它过完了浮点门、跑完了 FinishForkChild，走的是同一个出口的恢复序列，rax 的清零排在恢复之后的最后一步，所以孩子从 fork 里拿到的是 0，细节我们在 [进程的复制与替换](proc/fork_exec.md) 一页里讲过。

## SyscallFrame：任务在内核里的用户寄存器文件

SyscallFrame 是十六个四字、128 字节的定长结构，摆的次序我们从头数起，user_rsp 坐在 +0 的位置，跟在后面的是 rcx、r11、rax、rdi、rsi 与 rdx，r10、r8、r9 的三个字也都在列，凑成的是十个 ABI（应用二进制接口）字，垫底的六个 callee-saved 是 rbx、rbp，r12 到 r15 的四个也垫在底下。偏移的旁边有 static_assert 看着，汇编读的槽与 C++ 断言的数只能一致，对不上的时候编译就过不去。

```cpp
// kernel/syscall/syscall.hpp
static_assert(offsetof(SyscallFrame, rcx) == 8, "entry banks rcx at +8");
static_assert(offsetof(SyscallFrame, rax) == 24, "the number sits at +24");
static_assert(offsetof(SyscallFrame, rbx) == 80, "the rbx lesson stays pinned");
static_assert(offsetof(SyscallFrame, r12) == 96, "r12 closes the sixteen-word frame");
static_assert(sizeof(SyscallFrame) == 128, "sixteen quadwords of frame");
```

我们挑两个摆法上的决定说清来由，头文件的注释都写了。user_rsp 坐在 +0 的位置，而不是只待在 gs:8 里，因为 gs:8 是中转的座位，下一任任务一进门就把它占了，出口装回栈的时候靠的是帧本身，帧里存的那个才是靠得住的。第二个决定是全量寄存器的上帧，注释给的来由是信号交付，信号交付的改写是整帧的，改写完的任务就直接回到了 ring 3，帧就是任务停在内核里的用户寄存器文件本体。顺带的还有一句，帧指针存在了 gs:24，fork 从那里把父亲被中断时的整帧读走，孩子栈面上的第一帧也就照它摆了，这一路的细节在 fork_exec 页。

处理函数的脸面我们在 `syscall_internal.hpp` 里对过，它是统一的，定义的 Handler 拿的是 `const SyscallFrame&`，返回的是 long long。注释把权限的边界写明了，表面对外许诺的是只读，绝大多数处理函数是只读不写的，合法改写帧的只有 exec 与 sigreturn 两个，它们在自己的函数体里拿回了可写，分发表对其余函数的只读许诺就守住了。摆放的惯例是系统调用一份文件，sys_read.cpp 的旁边配 sys_read.hpp，各家的私有帮手住在自己的编译单元里，跨了文件的只有 Word 别名、Handler 与几个公共帮手，它们的家都在 `syscall_internal.hpp` 里。

帧里的 r10、r8、r9 三个槽现在也有人读，mmap 的 flags、fd、offset 取的就是 r10、r8、r9，rt_sigaction 的 sigset 长度读的也是 r10。结构体注释里给 r10 与 r8、r9 标上的 “unserved yet” 已经是旧话了，代码已经走在注释的前面了，我们按代码记录。

## 挂表与分发

挂表的本体在 `kernel/syscall/syscall_dispatch.cpp`，写法是分两段的，我们跟着登记与填表两段走。一份 kRows 的 constexpr 数组把号码与处理函数成对登记，一个 constexpr 的 lambda 遍历它，把 440 个槽位的 std::array 填出来，登记过的槽坐着函数指针，没登记的槽是 nullptr。

```cpp
// kernel/syscall/syscall_dispatch.cpp
extern "C" long long InvokeSyscall(cinux::syscall::SyscallFrame& frame) {
    const unsigned long long kNumber = frame.rax;
    if (kNumber >= cinux::syscall::kSyscallCount || kHandlers[kNumber] == nullptr) {
        cinux::print::Println("[kern] syscall %llu unserved", kNumber);
        return -cinux::syscall::kEnosys;
    }
    const long long kResult = kHandlers[kNumber](frame);
    frame.rax               = static_cast<unsigned long long>(kResult);
    if (cinux::proc::DeliverPendingSignals(frame)) {
        return static_cast<long long>(frame.rax);
    }
    return kResult;
}
```

InvokeSyscall 是 C 侧的接活口，汇编把帧指针交到了它手里。号码越界或者槽是空的，它就打印出 `[kern] syscall %llu unserved` 的一行，答出去的是 -ENOSYS。号码有人接的话，它就调处理函数、把返回值写进帧的 rax，再问一次有没有要交付的信号，交付发生了就按帧里改写后的 rax 回去，没有交付的话按原值回去。信号的问题我们留在出口一节细读。

挂表里有不少一号多用的行，我们把常用的几类并在一起看。vfork 挂的是 HandleFork，头文件注释交代的口径是“fork held until exec，served as plain fork”，本实现不给 vfork 单独的语义，它的路就是 fork 的路。exit_group 走的也是 HandleExit 的路，lstat 与 stat 归的是同一个 HandleStat，枚举旁的注释把原由交代了，本实现的树里没有链接可跟，两个号码答的是同样的许诺。fdatasync 与 fsync 共用的是 HandleFsync，faccessat 与 faccessat2 共用的是 HandleFaccessat，poll 挂的是 HandlePoll，注释说它是 ppoll 的旧拼法。wait4 与 waitid 各自挂 `sys_wait.cpp` 里的两个入口。

另一批行登记的是 HandleUnsupportedProbe，十个号码的名单我们数过了，各个都是程序拿来探底的设施，问的是有还是没有，答案落在调用方的回退路径上比落在日志里有用。挂着的十个是下面这些：

- `clock_getres`
- `sched_getaffinity`
- `io_uring_setup`
- `mremap`
- `madvise`
- `getrusage`
- `umask`
- `set_robust_list`
- `rseq`
- `clone3`

它们跟没挂的槽的区别只在响法，没挂的号码打的是一行日志，挂了 probe 的号码安静地答 -ENOSYS，`sys_probe.hpp` 的注释把用意写明了，这样答是为了让调用方的回退路径接得住，我们也就把两边的响法都记下了。

最小的入口可以小到一行正文。HandleGetpid 返回的是当前任务的 tgid，HandleGettid 返回的是 pid，HandleGetppid 读的是 ppid()，三个号码背后的身份语义我们在 pid 一页里讲过，这里就不展开了。HandleYield 的正文也只有一行，它干的只是把 CPU 交回调度器，答出去的是 0，调度器怎么挑下一个人的问题，我们在 [进程与调度器](proc/process_and_scheduler.md) 那一页里读过了。内存族的代表我们挑的是 HandleMmap，六个字它全用上了，flags、fd、offset 三个字的来处是 r10、r8 与 r9，它检查的是长度非零、页对齐，prot 只认读写执行的三位，匿名与文件两种背景各走各的装饰路径，指名地址的要过 ReclaimUserSpan 清一遍场，不指名的由 find_free_spot 挑座位，区间登记进了 vmas，起始地址也就被答回去了，地址空间一侧的细节我们留给 [地址空间](mm/address_space.md)。

公共的走廊住在 `sys_common.cpp`，我们挨个认一遍。ErrnoFrom、CurrentTask、LiveSlot 与 FileDescriptorLimit 是里面的四件，LiveSlot 把描述符的范围检查与取活的打开文件合成一步，越界的时候答 kInvalidArgument，空槽的时候答 kNotFound，FileDescriptorLimit 取的是任务的 RLIMIT_NOFILE 软上限，再被文件表的容量封顶（`syscall_internal.hpp` 注释口径）。

## 用户指针：范围检查与搬运

用户给的每一个指针都是不可信的数，内核碰它们之前要过的手续我们数出来是两道。第一道手续查的是范围，`syscall_internal.hpp` 里的 UserRangeReadable 与 UserRangeWritable 是同一对条件，区间是非空的、起点是非零的、长度是不回绕的，起点的上限是 `kUserAddressLimit`（`0x0000800000000000`，用户半区的上界）。头文件注释把这层检查的能耐与限度都写了，它管的是边界，翻译的事归缺页的路径，指着空洞的指针照样会走到缺页的路径上去，这对函数担保的是内核的字不会被碰、计数器不会被绕回负数。

第二道手续管的是搬运，我们把 CopyFromUser 与 CopyToUser 的路径读一遍，它们走的路是同一套。uaccess 的两个文件管着内核与用户内存之间的每一字节，零长度的搬运直接算成功，范围检查过了关，后头跑的是一条 rep movsb（串搬运指令），内联汇编按开窗与否写了两份，两份各往异常表里埋了一对项。异常表是内核里成对登记出错点与修复口的表，搬运走到半路缺了页，落点就是配对的修复口，也就是埋下的一对项里的 fixup 字，它做的是把开着的窗关上、把判决清零，正常的出口与修复的出口在后头汇合。输出操作数装回来的地方是硬件走过的寄存器，搬到了多远调用方是看得到的，不过每个调用方都把弹回来的搬运当失败处理（uaccess.cpp 注释口径）。

窗说的是 SMAP，开窗的资格我们问的是 CPUID。stac 与 clac 在没有 SMAP 的处理器上会触发 #UD（非法指令异常），引导步里 ArmWindows(HasSmap()) 的回答存进 DoorState，搬运的时候看的就只有那个存下来的回答。纪律写在 uaccess.hpp 的注释里，窗口绝不横跨任何会阻塞或者调度的路段，要开的就开、搬完就关，等待的路段排在窗关了之后。

路径类的系统调用还共用一条走廊，CopyUserPath 把路径从 ring 3 抄进内核的存储，FoldUserPath 与 FoldUserPathAt 多做的是折叠，折叠的规则是单点去掉、双点弹上去一层，相对名落在调用任务的工作目录底下，输出按 fs::kFsPathMax 的容量给（`syscall_internal.hpp` 注释口径）。旧页说的 canonical address 校验，我们核对之后没有找到对应物，现在的许诺就落在了上面两道手续的组合上。

## 阻塞的公共形状：让出循环

后端忙起来了、数据还没到，是文件类系统调用最常见的等待，处理函数里反复出现的形状是一个让出循环，我们拿 HandleRead 把它读一遍。开头的检查都是便宜的，描述符越界的时候答的是 -EBADF，缓冲地址越过用户半区的时候答的是 -EFAULT，描述符槽里没有活的打开文件还是 -EBADF。然后是一张 1024 字节的中转缓冲（kReadStageBytes），一次读的请求数被它截住，后端最多交的就是这么多，注释把中转的用意写明了，后端永远见不到 ring 3 的地址。

循环体的分岔是这样的，我们顺着走一遍。打开时带了 O_NONBLOCK 且后端备了 try_read 的，走的是试一下就回的入口。读成了，CopyToUser 推过去的字节跨过了用户线，光标跟着也往前走了，返回的是字节数。读回来的错误若不是 kWouldBlock，或者打开时带了 O_NONBLOCK，翻成负 errno 的错误就交了回去。剩下的情况要查的是待决信号，查到了待决信号就答 -EINTR，没有的话就 yield 让出 CPU，回来的下一轮接着读。写侧的 put_until_settled 是同一个形状，`sys_write.cpp` 的注释把两条红线写了，把忙碌的写变成 EAGAIN 的只有 O_NONBLOCK，待决信号在任何时候都是能破局的。append 语义靠一次 stat 探明常规文件的身份，探明了再进循环。

中转缓冲的另一面我们照实记下，一次 read 交出去的最多是 1024 字节，调用方要更多的话就再发一次调用。这不是 POSIX 对 read 的许诺。现在的实现拿缓冲换的是隔离，缓冲换来的保证写在 sys_read.cpp 的注释里，后端永远不见 ring 3 的地址。

时钟族的底座是两个数，我们把它们认一遍。机器里有 HPET 的时候 MonotonicNanoseconds 问的是 HPET，没有的话就拿 Tick 的计数乘成纳秒，一拍的长度是 10 ms，折成纳秒的话是一千万。Realtime 是开机那刻的墙上时间加单调增量，而墙上时间只问过 RTC（实时钟芯片）一次，`sys_clock.cpp` 的注释交代了原因，硬件的读取慢，墙从那之后就只管往前走了。

睡眠这一类的公共工具都在 `wait_time` 里，我们挨个认。ReadTimespec 把用户空间的 timespec 抄进内核并当场校验，秒为负、纳秒为负、纳秒过界（到了十亿）都答 -EINVAL，失败时调用方的状态一个字都没动。ReadWaitDeadline 把可选的超时译成单调钟上的截止期，空指针的意思是无限期，译出来的是最大的无符号字。MillisecondDeadline 是给 poll 族用的，做的是同一种翻译，对象是有符号的毫秒数，负数在它那里的意思就是无限期。

HandleNanosleep 译出截止期之后进 wait_until 的循环，循环的三拍是看钟、查信号、让出。钟过了截止期，答出去的就是 0。有信号挂着的时候，相对睡眠的剩余时间被折回 timespec 抄给了用户，答出去的是 -EINTR。两样都没有的话就轮到 yield。有一处我们要照实说，这层睡眠的停车没有发生在定时器上，任务是让出去又被挑起来、靠反复看钟滚到截止期的，wait_until 的循环体就三句，没有等待队列的参与，绝对时钟的 clock_nanosleep 走的是同一个循环，剩余时间的外带是省掉的。

## 出口顺路的信号交付

InvokeSyscall 写完 rax 之后还有的一问，我们在这里细读，问的是 `kernel/proc/signals.cpp` 里的 DeliverPendingSignals。它每一轮找出的都是最低的未屏蔽待决信号，分岔按的是动作。动作是忽略的，跳过去找的是下一个。动作是默认处置的，终止类的走法是 die_by_signal 直接退场，信号号当退出字的时候保持原样，父进程 wait 读到的是死于信号而不是代码（signals.cpp 注释口径），停类的走法是 stop_current 把任务置停。装了处理函数的走的是帧改写。

帧改写的步骤我们跟着走一遍。交付的足迹落在红区（调用约定里函数不许碰的 128 字节）之下，SigReturnContext 按 16 的对齐放了进去，返回字坐在 context 的下方。context 里抄的是改写前帧里的全部寄存器，被中断的那次调用的返回值此刻已经在 rax 里，跟着一起抄了进去，rt_sigreturn 回来的时候按它恢复。返回字要么是动作自带的 restorer（SA_RESTORER 答应过的），要么是内核的蹦床页（mm::kUserMmapTop 之上的一页）。改完的帧里，rsp 指到的是返回字，rcx 装的是 handler，rdi 装的是信号号，rax 被清了零。改写发生过的 InvokeSyscall 会把帧里的 rax 当返回值交回汇编，sysretq 按帧把寄存器恢复了，rip 取的是 rcx 里的，于是任务落地的地点就是 handler 自己。

交付之前还有一道栈窗的检查，我们把两处都认一遍。stack_holds_delivery 要求的，是交付足迹整个落在注册的栈窗里，装不下的话就拒绝交付，任务领的死法是 SIGSEGV。signals.cpp 的注释把立场写了，栈浅到装不下一个信号帧的任务本来就在死路上，在这里拒绝的是内核，往自己登记之外写字的风险就没有了。sigreturn 一侧的 in_stack_window 做的是同一种检查，sys_signal.cpp 的注释说得更直白，context 活着的地方只可能是被中断的栈指针底下，别处的 context 是被弄坏的栈，弄坏的栈就是任务死掉的原因。

## 源码位置

- `kernel/syscall/syscall.hpp` — 编号表 SyscallNr、errno 词汇、SyscallFrame、SyscallStat 与 dirent64 的布局承诺、flags 词汇表
- `kernel/syscall/syscall_internal.hpp` — Handler 面、UserRangeReadable/Writable、路径走廊、LiveSlot 的声明
- `kernel/syscall/syscall_dispatch.cpp` — kRows 挂表、kHandlers 编译期数组、InvokeSyscall
- `kernel/syscall/sys_common.cpp` — ErrnoFrom、CurrentTask、LiveSlot、FileDescriptorLimit
- `kernel/syscall/uaccess.hpp`、`kernel/syscall/uaccess.cpp` — CopyFromUser/CopyToUser、SMAP 窗口、异常表修复
- `kernel/syscall/wait_time.hpp`、`kernel/syscall/wait_time.cpp` — timespec 校验与截止期翻译
- `kernel/syscall/sys_*.cpp` 与同名 hpp — 一调用一份的实现，本页读过的代表是 sys_read、sys_write、sys_getpid、sys_gettid、sys_yield、sys_mmap、sys_signal、sys_clock、sys_nanosleep、sys_probe
- `kernel/arch/x86_64/syscall.S` — SyscallEntry 与 ForkChildEntry
- `kernel/arch/x86_64/usermode.cpp` — EnableFastSystemCalls 与 STAR 的折叠
- `kernel/arch/x86_64/per_cpu.hpp` — gs:0、gs:8、gs:16、gs:24 四个槽的语义
- `kernel/proc/signals.cpp` — DeliverPendingSignals、帧改写、栈窗检查
- `kernel/proc/fpu_gate.cpp` — EnterFpuGate 与 ExitFpuGate
- `kernel/init/init_sequence.cpp` — fast system calls 引导步
