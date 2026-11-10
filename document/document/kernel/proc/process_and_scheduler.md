# 进程与调度器

## 功能概述

这一页我们把 `kernel/proc` 里一个任务的一生完整地走一遍。Task 是任务的控制块，状态机、身份字段和调度参数都长在它的身上。TaskBuilder 负责的是造任务，它从 PMM（物理内存管理器、管物理页的分配和归还）划页做内核栈，控制块则落在了内核的堆上，入口和栈的位置被写成一张能直接执行的图像。Scheduler 是调度器的本体，而它自己不碰寄存器，寄存器切换、页表切换和内存归还的事情，全部交给了注入进来的 SwitchSink，内核装的是汇编版，host 测试装的却是记录版。谁下一个上 CPU 的问题，我们交给注册进来的 SchedulingClass，注册的顺序就是挑选的顺序。时钟中断做的只是记录，真正的换人发生在每个设备中断的出口。进程怎么被复制和替换，我们看 [进程的复制与替换](fork_exec.md)，fork 与 exec 的那一页也重写过了。锁族怎么用 `prepare_to_wait` 和 `schedule_blocked` 这两步停车，我们会在阻塞一节里讲个梗概。

## 任务控制块 Task

Task 的定义在 `kernel/proc/task.hpp`，文件头的注释给它的定位，说的是一张寄存器图像、一份身份，再加上一个它自己拥有的栈。我们从状态机看起，因为后面的所有路径都在这几个状态之间搬任务。

```cpp
// kernel/proc/task.hpp
enum class TaskState : unsigned char {
    kReady,    ///< Queued, waiting for a CPU.
    kRunning,  ///< On the CPU right now.
    kBlocked,  ///< Parked on a wait queue, not schedulable until woken.
    kStopped,  ///< Halted by a stop signal; only SIGCONT or SIGKILL
               ///< from another task puts it back on the queue.
    kZombie,   ///< Exited with a parent alive; the body stays until reaped.
    kDead,     ///< Entry returned and exited; memory pending reclaim.
};
```

kReady 的意思是任务在某个调度类的就绪行里排队等 CPU。kRunning 说的是它正跑在某个核上。kBlocked 是它停在了等待链上，等锁、等 futex（用户态的锁字）、等 wait_child 的返回都算，别人还没把它唤醒的时候，调度器就不会再挑它了。kStopped 是它被停信号摁住了，能把它放回行里的只有两条路，也就是别的任务发来的 SIGCONT 或 SIGKILL，它自己是没资格救自己的。kZombie 则是它已经退出、父任务还在世的状态，它的尸体要留给父亲来收。kDead 就是事情都办完了的阶段，只差把内存还回去了。后面我们走的每一条路径，你都可以拿这几个状态去对号。

身份字段是一层套一层的，我们分开读。pid 由 Scheduler 在入册的时候发放，入册口 seat 的 initial_process 参数会把 1 留给用户的 init 进程，其余的从 next_pid_ 递增。tgid 是线程组的号码，它跟 pid 共用的是同一个号池，入册的时候 tgid 就等于 pid，每个任务在生下来的时候都是自己组的组长，之后 CLONE_THREAD 的克隆会被重新指回制造者的组，所以同一个进程里的线程调 getpid，拿到的都是同一个号。pgid 是进程组的号码，kill(-pgid) 和 wait 里 wanted 为 0 的组目标沿它走，fork 复制的是它，setsid 会换一个新的。parent 指针决定的是谁有资格等这个任务，ppid() 帮我们读出父亲的 pid，没有父亲的时候它答的就是 0。

调度字段我们只需要记两个。priority 是任务在自己类里的调度权重，数值小的排前面，头文件的注释说得很直白，读它的只有权重类（代码里的 PriorityClass），别的类是一概不读的。sched_class 指向这个任务排的那一行，留空的任务会坐到最早注册的类里，这个归属会在入册的时候一次性定下来。

剩下的字段我们按归属扫一眼，用到哪一站的时候，我们就回来看哪一站。ctx 是下一节的 CpuContext。stack_base 记的是栈的分配起点，释放的时候要还到这里，任务运行时手里拿着的任何栈指针都作不得数。space 和 vmas 是它退场时要一起带走的用户世界，CLONE_VM 的克隆给世界加一个持有者而不复制，最后一个走的负责收尾。files 是它自己的文件表，CLONE_FILES 的克隆会改为共享制造者的文件表。comm 是十六字节的进程名，转储和 procfs 读的就是它。signals 装的是动作表、屏蔽集和待决集。另外的四个自链节点是 child_link、registry_link、sync_wait_link 和 futex_wait_link，分别坐进调度器的后代名册、全体名册、同步原语的等待链和 futex 桶的链，各用各的节点，所以为一个锁停车和被父亲等这两件事，是不会串到同一条链上的。

## TaskBuilder 与任务的出生

造任务走的是 TaskBuilder 的链式接口。set_entry 填的是线程的入口函数，这是必填的。set_name 填的是转储用的名字，名字的存储归调用者保管。set_parent 管的是认父，没认过父的任务，退出的时候是没人能等它的，也就直接走了回收。剩下的 set_priority 和 set_class，填的就是上面说过的两个调度字段。我们拿 build() 的执行顺序当线索：栈是按 kStackOrder=2 从 PMM 划出来的 4 个连续页（16 KiB），控制块用 MakeOwn<Task> 落在了内核的堆上，最后的五个地址写进 StartPlan，之后交给了 PrepareTaskContext。任何一步失败的时候，已经拿到的就全部退还，调用方拿到的是一个 nullptr。StartPlan 的五个地址，我们一个个对：trampoline 是任务的起点指令，entry 进的是 r12，exit_hook 进的是 r13，stack_bottom 是埋溢出标记的字，stack_top 则是最初的 rsp、十六字节对齐。

```cpp
// kernel/proc/task.cpp
void PrepareTaskContext(cinux::arch::CpuContext& ctx, const StartPlan& plan) {
    cinux::base::SetBytes(&ctx, 0, sizeof(ctx));
    plant_clean_fpu_image(ctx);
    auto* const kBottomWord = cinux::base::PtrAt<unsigned long long>(plan.stack_bottom);
    *kBottomWord            = kStackMagic;
    ctx.rsp                 = plan.stack_top;
    ctx.rip                 = plan.trampoline;
    ctx.r12                 = plan.entry;
    ctx.r13                 = plan.exit_hook;
}
```

这个函数只对调用者给定的地址做算术和一次写入，所以 task.cpp 能直接链进 host 测试，我们拿普通缓冲区就能驱动它。清零之后它铺了一张干净的浮点图像，x87 的控制字是 0x037F，MXCSR 的默认值是 0x1F80，任务第一次被 fxrstor64 恢复的时候，落进的就是确定的浮点状态。栈底那个字写的是 kStackMagic，它的值是 0xDEADC0DE，它的定义在 proc_config.hpp，事后我们靠这个字判断栈有没有跑过底。rsp 对到了栈顶，rip 对到了蹦床。蹦床 TaskTrampoline 的三拍很短，它的位置在 context_switch.S，sti 打开的是中断，call *%r12 跑的是入口，入口返回后再 call *%r13 走统一的退出口。一个仅仅是 return 的任务，也会体面地落进 exit_current。r13 指的退出口本身不返回，万一它返回了，紧跟的 ud2 会把机器当场拦下来。

## 上下文切换：CpuContext 与 ContextSwitch

CpuContext 的定义在 `kernel/arch/x86_64/context.hpp`。它的前八个槽是 callee-saved 寄存器 r15、r14、r13、r12、rbp、rbx、rsp、rip，后面跟的是一张 512 字节的 FXSAVE64 图像，让 x87 和 SSE 的状态跟着任务走，最后则是 fs_base 和 gs_base 的两个基地址字。我们看它为什么敢只存 callee-saved：切换发生在已知的调用边界上，那个点上 caller-saved 寄存器本来就死了，这是被调用者保存的调用约定白纸黑字保证的。每一条偏移的背后都有 static_assert 看着，汇编读的槽位和 C++ 断言的偏移只能一致，对不上的时候就直接编译失败，没有人再去手算偏移了。

```asm
# kernel/arch/x86_64/context_switch.S
ContextSwitch:
    movq %r15, 0(%rdi)
    movq %r14, 8(%rdi)
    movq %r13, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %rbp, 32(%rdi)
    movq %rbx, 40(%rdi)
    movq %rsp, 48(%rdi)
    leaq .Lresume(%rip), %rax
    movq %rax, 56(%rdi)
    fxsave64 64(%rdi)
    testq %rdx, %rdx
    jz .Lsaved
    movl $-1, (%rdx)
.Lsaved:

    movq 0(%rsi), %r15
    movq 8(%rsi), %r14
    movq 16(%rsi), %r13
    movq 24(%rsi), %r12
    movq 32(%rsi), %rbp
    movq 40(%rsi), %rbx
    movq 48(%rsi), %rsp
    fxrstor64 64(%rsi)
    jmp *56(%rsi)

.Lresume:
    ret
```

我们把两个指针的分工读清楚。rdi 指向 outgoing 的 ctx，rsi 指向 incoming 的。保存侧写进去的是八个寄存器的值，rip 槽里写的是 .Lresume 的地址，于是任务被切回来的时候，落地的地方就是 ret，整个 ContextSwitch 的返回就像它从没离开过。fxsave64 把浮点状态也收进了图像。第三个参数 rdx 不为空的时候，汇编往里写的是 -1，也就是把 outgoing 的 on_cpu 释放回谁都不在跑的状态，SMP（多处理器）下的回收等的就是这个字。恢复侧干的是反向装填，最后 jmp *56(%rsi) 直接跳进 incoming 的 rip，不经过 call、所以也不存在返回一说。

FS 和 GS 基地址的换装在 C++ 侧完成，也就是下一节 KernelSwitchSink 里的 swap_address_regs，汇编是不碰这一段的。这里我们只需要把分工看清：GS base 在任何特权级下指的都是 per-CPU 块，任务自己的 GS 值从 IA32_KERNEL_GSBASE 这个中转 MSR（模型专用寄存器）走，per_cpu.hpp 的注释把其中的纪律写得很完整。

## 调度器骨架：SwitchSink、名册与 seat

Scheduler 是 Meyers 的单例，全部成员走的都是常量初始化，满足零构造的规则。没装 sink 的时候它什么都不做，init(sink) 把四个动词擦成了函数指针表，装进去的就是四个函数指针加一个上下文指针，运行时是见不到模板的。我们直接看这个接缝长什么样：

```cpp
// kernel/proc/scheduler.hpp
template <typename Sink>
concept SwitchSink = requires(Sink& sink, Task& outgoing, Task& incoming) {
    { sink.switch_tasks(outgoing, incoming) } -> std::same_as<void>;
    { sink.enter_from_main(incoming) } -> std::same_as<void>;
    { sink.return_to_main(outgoing) } -> std::same_as<void>;
    { sink.reclaim(outgoing) } -> std::same_as<void>;
};
```

我们把四个动词各自管的事读一遍。switch_tasks 负责任务到任务的交接。enter_from_main 管的，是内核 Main 头一回交出 CPU 的时刻。return_to_main 负责的是在最后一个任务退干净之后，把 CPU 还回 Main 的手里。reclaim 负责还一个死任务的内存。调度器管的只是语义，名册、状态、挑选和回收时机都是它的话，寄存器、页表和内存归还都是 sink 的话，两边谁也不知道对方怎么干活。

内核装的是 task_runtime.cpp 里的 KernelSwitchSink，InstallKernelSwitchSink() 这么一调，spawn、yield、exit 操作的就都是真寄存器图像了。它的 switch_tasks 是从 arm_task_entry 开始的：把 TSS（任务状态段）的 rsp0 和 per-CPU 的 kernel_rsp 都指到 incoming 的栈顶，下次从 ring 3 打进来的时候就有栈可落了。CR3 是按需换的，带着用户世界的任务换它自己的根，没有的就回内核的根。随后的 swap_address_regs 换的是 FS/GS，把 incoming 的 on_cpu 写成本核的编号，最后一步是 ContextSwitch 的跳转，outgoing 的 on_cpu 在汇编里被释放成 -1。reclaim 的自旋等的就是 on_cpu 归 -1，因为另一个核可能还在这个任务的上下文里收尾，等到它归了 -1，4 页物理内存就按 kStackOrder 还给了 PMM，控制块也跟着被 delete 掉了。host 测试装的是记录版，我们在 test/unit/test_scheduler.cpp 里能看到 RecordingSink 把每次决策记成流再逐条断言，同一份目标代码不碰机器就验证了调度语义，这正是接缝存在的理由。

入册的口子就是 seat，我们跟着它的检查顺序走。它查的是 sink 已装、类已注册，空类的任务会被指到最早注册的类，任务指了一个没注册的类就会被 Check 当场拦下（Check 是这个内核的运行时断言，命中了就停机报告）。initial_process 为真的时候它会扫一遍名册、确认 pid 1 还没被占用、然后发 1，其余情况发的号都从 next_pid_ 递增。tgid 的取值就地等于 pid。状态在这里被置成了 kBlocked，任务上的是全体名册 tasks_，父亲还在的任务还要上后代名册 offspring_。runnable 为真的时候它会向自己的类入行，成功了才变 kReady。行满了也不炸、也不停机，新来的任务就留在了 kBlocked，这一次的拒绝会计进 ready_refusals()，源码注释对它的态度是满行算负载，停的只是这个新来的任务，机器继续跑它的。

调度器手里的名册一共有四条链，我们分开认。tasks_ 是全体在册的任务，无父的也在。offspring_ 是有父的后代，全机共用的是一张表，因为链表类型做不了它所链接的那个任务的成员，名册才搬到了调度器的身上。waiters_ 是停在 wait_child 里的父任务。retired_ 是栈还被别的核压着、等条件成熟再收的孤僵尸。观察名册的公开面孔有 for_each_task、with_task、snapshot_by_pid 和 nth_group_leader，全都是在队列锁下走访的，snapshot_by_pid 只把值拷了出去，指针是不出锁的。

这个内核是没有专门的 idle 任务的，我们往后翻 kernel.cpp 就能看到 idle 由谁当。BSP（引导处理器）的 kernel Main 在一切装好之后进一个循环，循环里的节奏是这样的：cli 关掉的是中断，run_until_done() 把行里的任务跑干，EnableIrqAndHalt() 做的是开中断再停住。run_until_done 的做法是从行里挑任务、用 enter_from_main 交出去、回来之后再挑，行干了这个调用才返回，随后 Main 停在 hlt 上等它的中断。AP（其余的应用处理器）在 ApMain 末尾的循环里做同样的事，EnableSecondaryDispatch 发布了之后，它们也开始了自己的 run_until_done。没活的时候每个 CPU 都停在自己的等待点上，调度器是不需要为此造一个任务的。

## 调度类：SchedulingClass

这层接缝出现之前的调度器是一根硬编码的 FIFO 环，谁下一个上 CPU 是由 pop 说了算的，sched_class.hpp 的注释记着这段来历。SchedulingClass 把这个决定权交了出去：每个类自持的是一条就绪行，容量是 kReadyQueueCapacity=64 的槽数组，要向调度器回答的是四个问题。enqueue 问的是收不收一个新就绪的任务，行满了就答 false。pick 问的是交出下一个想上 CPU 的任务并从行里拿走，没货的时候就答 nullptr。remove 问的是把还在行里的任务收回去，停信号发送的时刻用的就是它，只有排它的类才知道任务坐在哪个槽里。wants_rotate 问的是跑着的任务满 slice_ticks 拍之后该不该换人，什么算一片也由类来定义。实现方还要保证的是 pick 不阻塞、不分配，我们日后写自己的类时要把两条硬边界守好。

挑选的顺序就是注册的顺序。pick_next 从最早注册的类问起、头一个交出任务的赢，全空了才轮到下一个。install_class 遇到同一个类注册两遍的时候是无操作，bring-up 的路径不用设防，注册数超过 kMaxSchedulingClasses=4 的时候，拦它的就是 Check。没指名类的任务坐的是最早注册的类。我们回头看 kernel.cpp 的装配：DefaultFifoClass 装完之后接着装的就是 PriorityClass，于是裸任务全落在了 fifo 上，这正是旧调度器的行为，所以普通世界不用改一个字、照旧继续跑。

sched_class.cpp 里的现成类有两个。fifo（代码里的 DefaultFifoClass）的 pick 走 take_front，从队头拿走的就是最早的可用者。权重类（代码里的 PriorityClass）的 pick 走 take_best_priority，我们把它的挑法看一遍：

```cpp
// kernel/proc/sched_class.cpp
Task* take_best_priority() {
    unsigned long long best = count_;
    for (unsigned long long i = 0; i < count_; ++i) {
        if (available(*slots_[i]) &&
            (best == count_ || slots_[i]->priority < slots_[best]->priority)) {
            best = i;
        }
    }
    return best == count_ ? nullptr : take_at(best);
}
```

priority 的数值小的排前面，比较用的是严格小于，最早到的平局者守得住自己的 best 位，所以在我们眼里平局总是给最早到的人。available 跳过的是 on_cpu 指着别的核的任务，SMP 下一行就绪行是全机共享的，正在别的核上跑的人就不能再被挑走了。两个类对一片的回答是一模一样的，两边用的判断都是 slice_ticks >= kTimeSliceTicks，常量 kTimeSliceTicks 的值是 2，在 100 Hz 的节拍下，一片的长度就是 20 ms。

饿死这件事我们得照实说，源码的注释也没打算藏：权重类不做老化、不做提升、平局之外没有任何救济，源源不断的权重高的工作，会把权重低的任务饿死。要用它的项目就得接受这个换法、或者去注册一个更聪明的类。我们把这个意思原样保留在文档里，不做任何的修饰。

## 时钟与抢占

节拍的来源一共有两条路，我们找出它们从哪来、再看它们都通向哪个函数。IRQ 线 0 上的 heartbeat 让 Tick::on_interrupt 计数，然后把节拍报给了 on_timer_tick。今天驱动这根线的设备是 PIT，它的代码在 kernel/driver/pit/pit.cpp，挂进 Tick 的活由 init_sequence.cpp 干，不过 Tick 服务特意做了类型擦除、连设备名都不认，换了芯片也不动消费者。本地 APIC 的时钟向量则是直接调 on_timer_tick。函数的本体只做记录、不做切换：

```cpp
// kernel/proc/scheduler.cpp
void Scheduler::on_timer_tick(unsigned long long now_ns) {
    if (now_ns >= next_real_deadline_ns_.load(std::memory_order_acquire)) {
        expire_real_timers(now_ns);
    }
    if (current() == nullptr || !preemption_.load(std::memory_order_acquire)) {
        return;
    }
    if (current()->sched_class->wants_rotate(++local().slice_ticks)) {
        local().slice_ticks = 0;
        local().rotate_due  = true;
    }
}
```

前半段是 real timer（进程的 alarm 定时器、到点发 SIGALRM）的到期检查，我们顺带看了它一眼：到了期的任务挂上 SIGALRM 的待决位，动作表里把 SIGALRM 设成忽略的任务除外，它们到期走的只是清零或按间隔续期，全程都在队列锁的保护下。后半段问当前任务的类 wants_rotate(++slice_ticks)，答 true 的时候就清零拍数，置起 per-CPU 的 rotate_due 之后再返回。中断的服务到此就结束了、谁也没有被切走。

真正提问的是 maybe_preempt，它被安在每个设备中断的出口上。irq_stubs.cpp 里调它的出口有三个，管设备的 irq_entry 是一处，管本地向量的 local_entry 是一处，管消息的 message_entry 也算一处。它进门后的头一件事是查一串否决项，我们把清单读一遍：rotate_due 没置、当前没有任务、抢占开关没开、preempt_depth 非零、SpinLedger 的登记表深度非零、queue_depth 非零，任何一条命中了它就返回、被中断的任务由 iret 原样送回，标志位是 iret 恢复的，IF 的故事不需要补丁。全过了它才把当前任务重新排回行里，行满了就继续跑、计一次拒绝，然后 rotate_from_interrupt 挑出下一个接班的人、switch_tasks 接力完成切换，回来之后的收尾交给 reap_pending。

set_preemption 的默认值是关，成员 preemption_ 的常量初始化就是 false。头文件的注释把原因写得很直白：堆和 PMM 是还没受保护的共享状态、同步站把它们武装起来之前，只该跑那些任务不碰共享结构的抢占演示，PreemptGuard 的计数也从同一个开关里取食。当前 kernel Main 在装完两个类和内核 sink 之后调了 set_preemption(true)，位置在 kernel.cpp 的 Main 里。我们读这一行的时候要记得，它背后站着的问题是共享结构的保护做好了没有。

PreemptGuard 是 preempt_disable 和 preempt_enable 的 RAII 壳，可以一层一层地嵌套，出了作用域就自动放掉了。持着它的期间中断出口不换人，锁族是它的头一个消费者。NoRescheduleGuard 是它的近亲，管的是 schedule_blocked 的推迟，我们在 host 的角色扮演测试里会见到它，靠它把一个标了等待的任务留在测试栈上观察。

## 阻塞、唤醒、退出与回收

阻塞被有意分成了两步，我们把两步的分工说清。prepare_to_wait 在调用者还持着等待锁的时候把当前任务标成 kBlocked，这时候它是不会切换的，锁也还在调用者的手里，名册上你的状态却已经是停着的了。schedule_blocked 的真正切换要等等待锁放掉之后，两步之间进来的唤醒是不会丢的，因为 unblock 看到的是 kBlocked 的你，它会把 kBlocked 的你变回 kReady、送回行里，等你真正切走的时候，你已经是就绪的了。block_current 是两步合一的便捷口，sync.cpp 和 futex.cpp 的锁族用的是分成两步的这一对接口，锁的所有权移交就能精确地落在两步之间。

停车这件事是有纪律的，我们把红线记下。纪律覆盖了四个入口、从 block_current 到 schedule_blocked，再到 wait_child 的停车点和 stop_current，它们查的都是同一句话：DefaultSpinLedger().depth() == 0，拿着自旋锁睡觉是 Check 级的错误。这里的取舍是宁可当场响亮地炸，也不让持锁的任务消失在等待链上。

unblock 做的只是一件事，把一个 kBlocked 的任务入行变 kReady。我们看它行满时怎么处理：这一次的唤醒会被丢弃并计数，睡眠者保持着 blocked 且自洽的状态，下一个唤醒事件要等行腾出了位置再落。丢掉的只是一拍，机器还在转它的。

exit_current 扮演的是退场口，入口函数自己返回的时候，蹦床的 r13 也通到这里。我们把它的执行顺序整段走一遍。开头是结自己的私事：vfork 的完成字被 finish 掉，clear_child_tid 记下的用户字清零并发 futex 唤醒，等这个线程结束的对接方，等的就是这一下。然后是 files 的放手，reap_pending 把之前攒下的尸体收一轮。接着要做的是在队列锁下安顿后代：找得到活着的用户 init（pid 1 且持有用户世界）的时候，孩子就交给了它收养，其中已是僵尸的那些还会让收养者接住 SIGCHLD，等着它们的收养者也会被从等待里捞起来。找不到收养者的时候，孩子就被改成了无父，已经是僵尸的孙辈直接出了册，栈还被别的核压着的进 retired_ 等条件。再往后就轮到了它自己，这里的条件判据是有线程组的父亲且自己是组长（pid 等于 tgid），条件成立的时候它变 kZombie、给父亲发去 SIGCHLD，父亲若还停在 waiters_ 的名单里就把它捞起来。线程组的成员（pid 不等于 tgid 的那些）走的是另一条路，它们是永不僵尸的，直接出了册、置成 kDead、进了 pending_reap，组里用制造者世界的 join 字回答外界，wait 对它什么都答不上来。无父的任务走的同样是直通回收。末尾的 pick_next 负责选出接班人，有人接班的时候由 switch_tasks 交接、没人就 return_to_main 回 Main，回收的收尾在切换之后由 reap_pending 补完。

wait_child 是父亲这一侧的收尸口。我们把 wanted 的语义一层层排：-1 的意思是任意孩子，0 是自己组里的孩子，小于 -1 是组号取负的那个组里的孩子，正数是指名道姓的孩子。线程组的成员不合格，take_zombie_child 扫后代名册的时候，会直接跳过 pid 不等于 tgid 的。找到匹配的僵尸就摘下来，回执 ChildReceipt 里带上它的 pid 和 exit_code，尸体则交给了 reclaim，答的自然是 true。一个匹配的孩子都没有的时候它就答 false，这是组定向 wait 只剩别组孩子时的正确回音。有匹配的活孩子但还没退出的，no_hang 为真就给一个零 pid 的回执、调用方拿它答 0，这不算是什么错误。否则父亲停进的就是 waiters_，等的是孩子退场来叫，假醒了发现没匹配就再停一次、白醒也不吃亏。

停与续是信号的落点，我们用一句话带过：stop_current 把当前的任务置成 kStopped 并切走，withdraw 把还在行里的任务从它的类里撤出来置停，resume 把停着的任务放回行里。被停的任务不会被调度，所以它递交不了自己的死讯，发送者的 send 是唯一能让停机任务回到行里的路径。

## 线程安全与锁

QueueGuard 是调度器专用的守锁方式，我们跟着一次拿锁放锁走一遍。拿锁的时候 SaveAndDisableIrq 关中断，per-CPU 的 queue_depth 记嵌套，最外层才会真的去拿 queue_lock_（一把 SpinMutex），放锁的时候最外层解开、期间若置过 notify_pending，放锁之后干的事就是 NotifyRunnable()，把新就绪的工作广播给别的核。动名册和动行的路径都要过 QueueGuard 的检查，重入了也不会重拿锁。

current() 的读取只在 IrqGuard 下发生，读的是 per-CPU 的 current 指针。原子量我们数了三处：preemption_ 的类型是 atomic<bool>，next_real_deadline_ns_ 管 real timer 的下一站，Task::on_cpu 的装卸用的是 acquire 和 release 的配对，一个任务在哪个核上跑、跑完了没有，跨核的问答靠的全是这个字。

这一页在上面这些之外还剩一条停车的纪律，我们前面写过：SpinLedger 登记表的深度非零时不停车、不换人。SpinLedger 本身长什么样、锁族怎么靠它防死锁，我们看 [同步原语](sync.md)那一页，它已经按新实现重写过了，SpinLedger 是一张登记持锁层数的表，这里就不再重复展开了。

## 源码位置

- `kernel/proc/task.hpp`、`kernel/proc/task.cpp` — Task 控制块、StartPlan、PrepareTaskContext、TaskBuilder
- `kernel/proc/task_runtime.cpp` — TaskBuilder::build、KernelSwitchSink、InstallKernelSwitchSink
- `kernel/proc/scheduler.hpp`、`kernel/proc/scheduler.cpp` — Scheduler 语义、抢占、阻塞唤醒、exit 与 wait
- `kernel/proc/sched_class.hpp`、`kernel/proc/sched_class.cpp` — SchedulingClass 接缝、fifo 与权重类的实现
- `kernel/proc/proc_config.hpp` — 栈页数、栈底标记、行容量、时间片、类数上限等常量
- `kernel/arch/x86_64/context.hpp`、`kernel/arch/x86_64/context_switch.S` — CpuContext、ContextSwitch、TaskTrampoline
- `kernel/arch/x86_64/per_cpu.hpp` — per-CPU 块：current、slice_ticks、rotate_due、queue_depth 等
- `kernel/arch/x86_64/irq_stubs.cpp` — heartbeat 与三个中断出口上的 maybe_preempt
- `kernel/kernel.cpp` — 类注册、sink 安装、抢占开关、Main 的 idle 循环
- `test/unit/test_scheduler.cpp` — RecordingSink 与调度语义的 host 测试
