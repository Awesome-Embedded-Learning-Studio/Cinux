# 进程的复制与替换：fork、execve 与 wait

## 功能概述

这一页我们把一个进程怎么被复制、怎么被换成另一个程序、退出之后怎么被父亲收回，按当前源码完整地走一遍。我们的分工是这样的：fork 与 clone 负责造副本，它们的本体在 `kernel/proc/fork.cpp` 与 `kernel/proc/clone.cpp`，系统调用的入口在 `kernel/syscall/sys_fork.cpp` 与 `sys_clone.cpp` 里。副本的地址空间靠写时复制（CoW）跟父亲共享页面，我们的引擎住在 `kernel/mm/cow.cpp` 里。execve 负责把当前任务的世界整个换掉，它的本体在 `kernel/syscall/sys_exec.cpp`。退出与回收落在 `kernel/syscall/sys_exit.cpp` 和调度器的 `exit_current`、`wait_child` 手里。Task 的字段、调度器的名册和 sink 接缝，我们在[进程与调度器](process_and_scheduler.md)里已经走过一遍，那一页是这个模型的地基，我们只在用到的地方回指它。

得提一句旧版的来历。这一页的上一版讲的是 `kernel/proc/process.hpp/cpp` 加 TCB 的叙事，我们核过源码树，那批文件已经不存在了，wait 的实现也从 `sys_waitpid` 搬到了 `sys_wait.cpp` 里的 wait4 与 waitid。所以这一版我们按现在的源码重新搭叙述，旧版只留着告诉我们读者原来读到过什么。

## fork：一个不落座的副本

系统调用的入口写得很薄，我们把 `sys_fork.cpp` 里全部的动作看完：我们调 BuildForkChild 造出孩子，跟着我们把孩子 seat 进调度器，给孩子领上了属于他的 pid，最后把孩子的 pid 当返回值交给父亲。孩子那一侧拿到的 0 不是我们的路径写的，它出自汇编：孩子的入口 ForkChildEntry 在 `kernel/arch/x86_64/syscall.S`，等它恢复完了寄存器之后，执行的便是 `xorq %rax, %rax` 这一句，然后就跟着 sysretq 回到我们的 ring 3。于是在同一次 fork 的调用里，父亲从 HandleFork 的返回值里拿到 pid，而孩子从自己的入口里拿到 0，两边的差别被压缩在一个寄存器里。

```cpp
// kernel/syscall/sys_fork.cpp
long long HandleFork(const SyscallFrame& frame) {
    proc::Task* const kChild = proc::BuildForkChild(reinterpret_cast<unsigned long long>(&frame));
    if (kChild == nullptr) {
        return -kEnomem;
    }
    proc::Scheduler::self().seat(*kChild);
    return static_cast<long long>(kChild->pid);
}
```

造孩子的活全在 BuildForkChild 里，我们跟着它的顺序往下读。它进门时挂上的 IrqGuard 把中断关掉了，然后从 frame_address 读出父亲被入口汇编存下来的 SyscallFrame，把 r12 到 r15 的四个用户寄存器抄在一边。开造之前有一道检查值得我们停下来看，源码的注释把动机写得很清楚：一次 spawn 在开头就被拒绝，比它死在内核栈的中途要好，因为造孩子的调用链本身就跑在即将交出去的那根栈上，栈用掉了四分之三以上的时候就会当场停下。

```cpp
// kernel/proc/fork.cpp
void guard_spawn_depth(const Task& spawner) {
    unsigned long long rsp = 0;  // NOLINT(misc-const-correctness) written by the asm output
    asm volatile("movq %%rsp, %0" : "=r"(rsp));
    const unsigned long long kUsed = spawner.stack_base + kStackBytes - rsp;
    cinux::base::safety::Check(kUsed < (kStackBytes / 4) * 3,
                               "spawn refused: the caller's kernel stack is nearly spent");
}
```

过了上面的检查，孩子需要的三大件就逐一到位了。地址空间走 `mm::cow::CloneSharedSpace`，从父亲的世界里克隆出一个每一页都共享写时复制的副本，VmaStore（管地址区间的登记表）跟着 `clone()` 出一份，这两步里的任何一步要是拿不到内存，函数就立刻返回了 nullptr 的答案。内核栈是按 kStackOrder 从 PMM 划出来的，控制块是用 MakeOwn 落在内核堆上的，我们分配要是失败了，就把已经划出的栈原路退还。fork 对内存紧张的回答是一个干净的 -ENOMEM，它从不交出半个残废的孩子。

孩子的上下文是我们拼出来的。ctx 是整个从父亲那里抄来的，四个用户寄存器换回帧里存的那几个值，rip 被改写成 ForkChildEntry 的地址，rsp 指到新栈顶往下让出的一个 SyscallFrame 位置，父亲的那一帧整个 CopyBytes 过去，于是孩子醒来时的栈面跟父亲进系统调用时一模一样。浮点图像的来源，fork.hpp 的注释专门交代过：用的是父亲的 entry_fpu，也就是父亲上一次切换时存下来的图像，而此刻寄存器里的实时状态是不管的，我们的取舍跟引擎更早的版本的形状是一脉相承的（“第一幕”这个叫法出自源码注释自己的口径，cow.hpp 与 sys_exec.hpp 的注释都这么称呼，本页的口径也就照它沿用了）。

继承的清单我们扫一遍就够。信号动作表和屏蔽集复制的是父亲的，pgid 和 sid 也带了过来，cwd、exe、控制终端、comm、rlimits、uid、euid、gid、egid、保留能力位、附加组全都复制到位了，文件表走的是 `files->clone()`，所以我们拿到的是一份独立的副本，描述符指向同一批打开的文件，偏移也是跟着走的，而表本身各是各的。CLONE_THREAD（clone 的一个标志位，我们到下一节才会遇到它）在这里没有出场的机会，fork 出来的孩子 tgid 要等 seat 的那一刻才定下来，seat 里执行的是 `task.tgid = task.pid`，每个 fork 的孩子生下来都是自己线程组的组长。

最后的分工，fork.hpp 的标题注释写着：孩子回来时是 unseated 的。BuildForkChild 只负责把孩子的躯壳拼好，pid 是由调用方通过 seat 发放的，调度器是唯一发身份的地方。我们看 HandleFork 的三行时，之所以造孩子和入册分成两步，答案就在我们前面读过的 seat 里。

## clone：fork 加上共享开关

clone 的入口在 `sys_clone.cpp` 的 HandleClone，我们把它的做法读完就清楚了，它的思路是把 fork 当底座，再用标志位把孩子的几条腿换成对父亲部件的引用。proc/clone.hpp 的注释把关系说得很直白：基础孩子完全按 fork 的方式造，写时复制的世界、克隆出来的文件表、新栈上的帧拷贝一样不少，然后标志位就覆盖上场了，CLONE_VM 把孩子的地址空间指到父亲的，CLONE_FILES 指到父亲的描述符表，而 CLONE_SETTLS 在 ring 3 的第一条指令跑起来之前，就把线程指针装好了（也就是 FS 基地址）。

入口的检查顺序我们过一遍。标志位要过一张 served 掩码的过滤，掩码外的位整个被拒绝，注释里的说明很直接，它的态度是宁可全拒，也不做半截的服务。低八位被当成退出信号读了出来，它要么是 0，要么是 SIGCHLD，而且跟 CLONE_THREAD 同用时会被打回，因为线程的退出是不发信号的。用户栈指针必须在用户地址范围里，CLONE_SETTLS 的 tls 参数得是规范地址，parent_tid 和 child_tid 的两个落点要过 UserRangeWritable 的可写检查，写不了就答 -EFAULT，这些检查全都排在造孩子的前面。

有一样东西必须由入口交给建造者而不能省，它就是 ClonePlan 里的 user_stack。clone.hpp 的注释解释了原因：帧拷贝里存的还是父亲的栈指针，一个线程要是在那个地址醒来，踩的就是制造者的栈，所以调用方给的新栈顶要单独写进孩子的帧里。BuildCloneChild 在 fork 孩子之上做的正是这一句 `child_frame.user_rsp = plan.user_stack`，我们再把三条的覆盖读完：CLONE_VM 的时候，孩子的 space、vmas 与 user_root 这三样，都直接共享父亲的。CLONE_FILES 的时候，共享的是 files。CLONE_SETTLS 的时候，执行的是 `ctx.fs_base = plan.tls`。共享的部件拿的是引用计数，所以制造者可以死在前面，而世界会撑到组里最后一个成员离开。

入册的节奏，fork 和 clone 是不一样的，我们对照着看。HandleFork 里的 seat 默认是 runnable 的，孩子进了就绪行直接排队。HandleClone 里执行的是带 runnable=false 的 seat，孩子以阻塞态入了册，把 tgid、tid 地址这些身份的活干完之后才 unblock。中间的两句值得我们看一眼：CLONE_THREAD 的时候执行 `kChild->tgid = CurrentTask()->tgid`，孩子被重新指回制造者的组，从此组里调 getpid 拿到的都是同一个号。vfork（一种父亲停下来等孩子的创建方式，等的是孩子换好程序或者退出的那一刻）标志的处理也在 clone 的路径上，孩子会挂上一个属于 vfork 的 Completion 完成字，父亲在 unblock 完孩子之后就停在 `done.wait()` 上了，等到孩子 execve 成功或者退出了，把完成字给 finish 掉了，而父亲这才继续往下走。sys_vfork 这个系统调用号本身走的是 HandleFork，我们核过 syscall_dispatch.cpp 的表，带 vfork 语义的其实是 clone 的标志位。

## 写时复制怎么接

fork 敢只克隆页表而不克隆物理页，靠的是 `kernel/mm/cow.cpp` 里这套引擎。我们读 cow.hpp 的开头注释，它交代了这套引擎的设计立场：这套引擎为 fork 打底，但是建出来之后并不依赖 fork，任何映射都可以被标记成共享的，而对共享页的写错误会把页面重新变成私有的。每帧一个使用计数管住了“还有谁在用这一页”的问题，注释特意点了一句，引擎更早的版本没有这个计数，所以父亲退出时会把孩子还在读的页还回分配器，那是一个真实存在过的缺失。

标记的一侧是 MarkPageShared，我们看它做了什么。它走到了叶子表项，把页面的写权限摘掉，装上软件的 kCow 位，再给物理帧的计数加一。已经被标记过的页再标记一次就直接答 true，所以它是幂等的。克隆世界走的是 CloneSharedSpace，它从用户半区的根表开始逐层往下走，把每个在场的 4 KiB 叶子用同样的物理帧映射进孩子的空间，两边都各过一遍 MarkPageShared 的标记，计数就归了二。设备页是个特殊的例外，share_leaf 里查到 VMA 区间的 kind 是 kDevice 就直接返回，因为设备寄存器不能这么共享。用户半区里要是出现了大页，就会当场被 Check 拦下来而停机，注释的说法是任何映射用户内存的东西都不该造大页。走完之后还有一步容易漏的：如果当前的 CR3 正指着父亲的根，我们就要重装一次 CR3，把 TLB 里那些还带着写权限的旧表项冲掉。

写错误的处理是 TryResolvePageFault，它向异常报告器注册为缺页的解析器，把它装进去的活归 InstallResolver。我们看它的判定链：它只认写错误，别的错误原样放回给报告器。然后走到出错地址的表项，要求它是在场的、不可写的、带着 kCow 的，三个条件缺了任何一个都不接。接下来是整条路径里最有意思的岔口：

```cpp
// kernel/mm/cow.cpp
const unsigned long long kTarget  = walk::EntryTarget(kLeaf);
const unsigned long long kCompany = release_share(kTarget);
if (kCompany == 0) {
    *kSlot.entry = cinux::arch::page::MakeTableEntry(kTarget, private_flags(kLeaf, true));
    cinux::arch::InvalidatePage(address);
    return true;
}
```

计数减一之后要是归了零，说明写错误的一方已经是最后一个共享者，那就不复制了，我们直接把写权限还回来，一页的拷贝都省了下来。还有同伴的时候才走复制的路，PMM 划出一个新的帧，4 KiB 的内容拷过去，表项换成私有可写的，InvalidatePage 冲掉本核的旧译。整条路径都跑在缺页的深度上，cow.hpp 的注释列了纪律：不睡眠，除一个帧之外的分配一概不做，除 PMM 自己的锁之外不拿别的锁，外面再罩上我们的 g_fault_gate 自旋锁和 IrqGuard。

拆除的一侧是 ReleaseFrame，我们看它管的是什么：世界退场时把叶子一页页交还的场景。计数里还有人的时候帧就留着，直到最后一个使用者才把它还回分配器的手里。也正是这个计数的功劳，父亲退出的时候带不走孩子还在读的页。

## execve：换一个世界

HandleExecve 是这一页里最长的一条路径，我们分四段走。头一段是把程序找到并搬进内核：FoldUserPath 把用户给的路径折进内核的缓冲，FileWorld 帮我们解析出节点，stat 拿到的是文件的大小，然后我们的 ReadFilePrefix 只读前 64 KiB。ELF 头和程序头表都在这个 64 KiB 的前缀里，64 KiB 的来历注释没有明说，但后面的加载器只从前缀里解析头部，段的内容是按文件操作延迟读的。

第二段是参数和环境的落地。这里有个值得我们核对的细节：sys_exec.hpp 的注释还说这是一个“单路径参数、不带 argv 和 envp”的教学形状，但正文早就不是了，stage_arguments 从帧的 rsi 和 rdx 里数出 argv 和 envp 的两条向量，预算按任务的栈 rlimit（内核给每个任务记下的资源上限，这里取的是栈大小这一项）算出来，条数达到 kMaxArgumentStrings 或者字节预算超了就用 -E2BIG 拒掉，每条字符串用 uaccess 分页拷进内核的暂存区。argc 为零的时候路径本身顶上当 argv[0]，这是单参数时代留下的遗风，它的影子在代码里还留着。

第三段里我们把新的世界搭起来。fresh 的 AddressSpace 和 ranges 的 VmaStore 都是新建的，跟旧的世界一点关系也没有。image_load_bias 决定的是装载基址：链地址已经就位的定基形状偏移取零，从零链起的位置无关形状拿 kUserExecImageBase 加一段页对齐的随机滑移，ASLR 从这里就开场了。LoadElfIntoSpace 把各个段铺进新的空间，GNU_STACK 段的标志决定栈可不可以执行，brk 的起点、mmap 的顶、栈的顶各拿一段随机滑移。程序头里指向解释器的时候，ReadInterpreterPath 帮我们读出路径，LoadInterpreter 把动态的链接器也装了进来，入口让位给了它。信号蹦床是用 MapSignalTrampoline 映进新空间的。auxv（内核在入口栈面上递给新程序的辅助向量，头文件之外的启动信息都装在它里面）里有一项叫 AT_RANDOM，它的十六个字节靠 RDRAND（x86 的硬件随机数指令）抽出来，机器要是没有 RDRAND 就答了空，auxv 里就干脆不带这一项了。

第四段是入口的栈面。我们把 argv、envp、auxv 连同程序名一起拼成一张入口图像，WriteEntryPicture 把它写进内核的缓冲，map_entry_stack 再一页一页映射到新空间顶部的栈区，栈区的大小取的是 rlimit 和可用空间里小的那个，太小的图像用 -E2BIG 拒掉。到这里新世界就齐了，接下来是换装的那组动作，我们动手的顺序不能乱：

```cpp
// kernel/syscall/sys_exec.cpp
kSelf->signals.reset();
kSelf->keep_capabilities = false;
kSelf->clear_child_tid = 0;
kSelf->files->close_on_exec();
note_program_identity(*kSelf, path);
// The dying worlds must not come apart while CR3 still walks them:
// park the old holds aside, switch, and let them go at scope end.
// NOLINTNEXTLINE(misc-const-correctness) the dying worlds go at scope end
base::Shared<mm::AddressSpace> old_space  = std::move(kSelf->space);
// NOLINTNEXTLINE(misc-const-correctness) the dying worlds go at scope end
base::Shared<mm::VmaStore>     old_ranges = std::move(kSelf->vmas);
kSelf->space                              = std::move(fresh);
kSelf->vmas                               = std::move(ranges);
kSelf->user_root                          = kSelf->space->root();
kSelf->space->activate();
```

我们在源码里读到的顺序是：信号动作表整个重置，保留能力位清掉了，clear_child_tid 也清了零，文件表里带 CLOEXEC 的描述符关掉，comm 和 exe 记上新程序的名字。旧世界的两个 Shared 被挪到局部变量里攥着，代码注释把原因写在头顶：CR3 还走在旧世界上面的时候不能让它散架，等切换完成了之后，作用域结束的时候我们再放掉。activate() 把 CR3 换到了新根，然后是收尾的一笔，入口帧被整个清零重写了，user_rsp 指向的是入口图像，rcx 放的是新程序的入口指令，r11 放 0x202 的用户标志，rax 也清了零，sysretq 执行了之后，ring 3 落地的地方就是新世界的第一条指令。execve 成功的时候，旧的执行流到此为止，返回到旧程序的路在这个函数里是不存在的。挂过 vfork 完成字的任务在这里把它 finish 掉，等着的父亲就此放行。

## wait：僵尸的收条

退出与回收的全景，我们在[进程与调度器](process_and_scheduler.md)的退出一节走过，这里只取跟 wait 接口的那几块。在 exit_current 的实现里，有父亲且 pid 等于 tgid 的组长变 kZombie，给父亲发了 SIGCHLD，父亲要是停在 waiters_ 里的话就把他捞起来。线程组的普通成员不走僵尸，直接出册进了 pending_reap，wait 对他们什么都答不上来。父亲死了的孩子交给 init（pid 1 且持有用户世界的那个任务）收养，找不到收养者的僵尸孙辈直接回收，栈还被别的核压着的那些进 retired 名单里等条件成熟。

接口的这一侧是 `sys_wait.cpp` 的 HandleWait4：wanted 从 rdi 读出来，按 int32 的宽度截断，注释里专门防了一手，libc 包装可能把寄存器的高位留零，wait(-1) 不能变成 4294967295 号的进程。WNOHANG 在 rdx 的低位上。wait_child 要是答了 false，说明既没有匹配的僵尸也没有合格的活孩子，我们回的就是 -ECHILD 了。拿到回执就把 exit_code 拷进用户的状态字，返回孩子的 pid。WNOHANG 且孩子没退的时候，回执的 pid 是零，调用方拿到的返回值是 0，这不算是什么错误。

waitid 是我们在 `sys_wait.cpp` 里看到的同一个 wait_child 套的另一层皮，它只收 WEXITED 和 WNOHANG 的两个标志，回执按 siginfo 的形状填 128 字节。

收尸的机制全在调度器的 wait_child 里，我们把它的循环读顺。每一轮都在队列锁的保护下扫一遍后代的名册，take_zombie_child 按 wanted 的语义找匹配：-1 是任意孩子，0 是自己进程组里的孩子，小于 -1 是组号取负的那个组，正数是指名道姓的 pid。扫的时候有一条硬性的限制要放在我们心上，pid 不等于 tgid 的任务直接跳过，线程组的成员永远进不了收尸名单，而这跟退出路径里只有组长变僵尸是同一枚硬币的两面。找到了匹配的僵尸，就把它从两条名册上摘了下来，回执里装上的就是 pid 和 exit_code，reclaim 当场把栈和控制块还掉了。有合格的活孩子但还没退的，WNOHANG 给的是零回执，否则父亲就挂进 waiters_ 的名单里停车，孩子退场时 exit_current 发的 SIGCHLD 加 unblock 把他叫回来，假醒了一轮没找到匹配就再停一次，循环就是这么转的。

```cpp
// kernel/proc/scheduler.cpp
waiters_.push_back(*kSelf);
kSelf->state = TaskState::kBlocked;
```

我们在停车前还有一道自旋锁的检查，DefaultSpinLedger 的深度非零就 Check 停机，拿着自旋锁睡觉在这个内核里是当场响亮地炸，而这里的纪律跟锁族共用的是同一句话。

## 与线程组的关系

我们把三条路径合起来看，线程组改写的位置就清楚了。clone 的 CLONE_THREAD 把孩子的 tgid 指回制造者的组，所以同组的 getpid 拿到的是同一个号。退出的时候只有组长有资格当僵尸，成员走的是直通回收。wait 的名册扫描跳过所有的成员，父亲等到的永远是组长。fork 自己是不产生线程组的，每个 fork 的孩子都是新组长，所以 fork、exec、wait 的经典链在只有 fork 的程序里感觉不到线程组的存在，是 clone 把三者的语义各改了一角。

## 源码位置

- `kernel/syscall/sys_fork.cpp` — HandleFork 的入口，造孩子、seat、返回 pid
- `kernel/proc/fork.hpp`、`kernel/proc/fork.cpp` — BuildForkChild，孩子的三大件、继承清单、栈深检查
- `kernel/syscall/sys_clone.cpp`、`kernel/proc/clone.hpp`、`kernel/proc/clone.cpp` — HandleClone 的检查、ClonePlan、标志位覆盖
- `kernel/mm/cow.hpp`、`kernel/mm/cow.cpp` — MarkPageShared、CloneSharedSpace、TryResolvePageFault、ReleaseFrame、InstallResolver
- `kernel/syscall/sys_exec.hpp`、`kernel/syscall/sys_exec.cpp` — HandleExecve 的完整换装路径
- `kernel/loader/loader.hpp` — LoadElfIntoSpace、入口图像与解释器装载
- `kernel/syscall/sys_wait.cpp` — HandleWait4 与 HandleWaitid
- `kernel/syscall/sys_exit.cpp` — HandleExit，exit 与 exit_group 共用
- `kernel/proc/scheduler.cpp` — exit_current、wait_child、take_zombie_child、detach_offspring、reap_pending
- `kernel/arch/x86_64/syscall.S` — SyscallEntry 的返回尾与 ForkChildEntry 的 rax 清零
