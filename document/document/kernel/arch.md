# x86_64 架构层

## 功能概述

这一页我们把 `kernel/arch/x86_64` 里各子系统都要借用的公共设施走一遍。这个目录里的文件各有各的去处：引导与长模式怎么起来归 [引导链](../boot.md)，时钟与中断链的心跳归 [PIT 定时器](drivers/pit.md)，早期输出走 [串口驱动](drivers/serial.md) 的那一页，显示走 [视频子系统](drivers/video.md) 的那一页，上下文切换的整套编排归 [进程与调度器](proc/process_and_scheduler.md)。我们这一页认领剩下的公共设施，也就是 CpuContext 与 FpuImage 的寄存器图像、per-CPU 块与 GS 基址的换装、中断向量与桩的架构侧、GDT 与 TSS（任务状态段）的两张段表，还有一层字级的硬件原语。整层有一个贯穿的纪律，我们把它摆在概述里：凡是汇编要按偏移读的内存，C++ 侧就有守着同一个数的 static_assert（编译期断言），两边的数只能一样，数一旦对不上断言就跟着碎、构建当场就失败了，所以谁也不需要手算偏移。

## 寄存器图像：CpuContext 与 FpuImage

CpuContext 的定义在 `kernel/arch/x86_64/context.hpp`，头文件的注释给它的定位只有一句话，它是 context_switch.S 的 C++ 镜像。结构的前八个槽是被调用者保存的寄存器，r15、r14、r13、r12、rbp、rbx、rsp、rip 挨个排开了，每个槽的偏移从 0 排到 56，每个槽的宽度是一个八字节。跟在后面的是 fpu 字段，它的起点在偏移 64，再往后是收尾的 fs_base 与 gs_base 两个字，整个结构的合计是 592 字节。注释说这些偏移是承重的，我们挨个数了，每个偏移都有对应的 static_assert 守着，结构一旦改了断言就跟着碎、构建当场就失败了，汇编读的槽位与 C++ 声明的偏移想漂移都没有门路。

只存八个寄存器的理由落在调用约定上，注释给的是约定里的标准答案：切换发生在已知的调用边界上，那个点上 caller-saved（调用者保存的寄存器）本来就死了，这是被调用者保存约定白纸黑字保证的。浮点的那半边叫 FpuImage，64 个 unsigned long long 恰好凑成的就是 512 字节，头文件用两条断言把体积与对齐都按住了，因为 fxsave64 的胃口是 512 字节，对齐的要求是 16 字节，我们连差一个字节的机会都没有。SaveFpu 与 RestoreFpu 是两个行内汇编的封装，把 fxsave64 与 fxrstor64 两条指令包了起来，包成 C++ 能直接调的函数。

```cpp
// kernel/arch/x86_64/context.hpp
struct alignas(16) FpuImage {
    unsigned long long words[64];
};

static_assert(sizeof(FpuImage) == 512, "FXSAVE64 writes 512 bytes");
static_assert(alignof(FpuImage) >= 16, "FXSAVE64 demands 16-byte alignment");
```

fs_base 与 gs_base 两个字是跟着图像一起走的，但是换装的动作不在汇编的手里。分工在注释里写明了，FS 基址由 C++ 侧在切换的前后搬动，GS 的情况更讲究一点，只要机器是跑着的，活的 IA32_GS_BASE 永远指着 per-CPU 块，任务自己的 GS 值走的是 IA32_KERNEL_GSBASE 这个中转，每一次切换的时候都要重新摆一遍。IA32 开头的两个名字都是 MSR（模型专用寄存器，CPU 的配置值住在这一类带编号的寄存器里），后文我们还会常见。换装的纪律归下一节，你到下一节能看到全套的动作，切换的编排归 [进程与调度器](proc/process_and_scheduler.md)，那里的走读已经把汇编逐行读过了，本页就不重复了。

这个文件还捎带了规范地址的一对常数。x86-64 的 64 位地址只有 47 位是实在的、其余的位一律跟第 47 位相同，满足这个形状的地址才叫规范地址。kCanonicalUserTop 与 kCanonicalKernelBottom 把用户半的顶与内核半的底定了下来，IsCanonicalAddress 管的是把关，判断一个值能不能安全地喂给基址寄存器。注释里的理由说得很直白，不合形状的值交给 wrmsr 引发的是 #GP（一般保护错误），CPU 不给我们返回任何错误码，问一句的机会都没有，所以判断要在写入之前做完，我们读代码的时候也要按这个顺序来。

## per-CPU 块与 GS 基址

per-CPU（每处理器一份的数据）块的定义在 `kernel/arch/x86_64/per_cpu.hpp`。为什么它要走 GS（x86 的段寄存器之一，长模式里我们把基址当指针用），答案写在头文件的注释里，讲的是系统调用的入口：特权跳变发生的时候页表可能整个换掉，指针类的手段大多会失效，段覆盖寻址却不受它的影响，所以 CPU 的 GS 基址是内核唯一一个不需要重装的指针，这个块就是它指着的地方。

块的字段我们按读者分成两半。汇编要摸的前四个四字是 kernel_rsp、user_rsp、scratch、syscall_frame，偏移的位置是 0、8、16、24，注释逐个给了用途：kernel_rsp 是系统调用进门要落的内核栈，user_rsp 替调用者保管的是用户栈，scratch 是一个四字的过路柜台，syscall_frame 记着入口帧的位置，fork 读它就能拿走整张的用户寄存器图像。注释还交代了一条来历，四个字的起点都是零，而且只由入口汇编书写。current 指针坐的位子是偏移 32，后面跟的是 cpu_id、apic_id、pending_reap，再往后是调度器的每核计数（slice_ticks、preempt_depth、no_reschedule_depth、queue_depth、rotate_due、notify_pending），最后两个原子计数器数的是本地时钟与重调度中断。static_assert 守的是五处偏移，汇编摸的四个字段之外，current 坐的 32 也在断言的单子上，头注释的原话是字段偏移承重，gs:0、gs:8、gs:16、gs:24 就是汇编写死的数字。

存储与访问是贴在一起的两侧。存储是静态的，per_cpu.cpp 里摆着一块 kMaxCpus 个块的全局数组，per_cpu_config.hpp 里的 kMaxCpus 把份数定成 8，数组的家安在 .data 节，对齐照着页的大小来，起步的事情交给 constinit。访问器 self() 的实现只有一行，读的是 IA32_GS_BASE 这个 MSR，把读到的地址直接当 PerCpu 用，访问器跟着装好的基址走，我们读它的时候不用挑核，谁在跑就答谁的块。

```cpp
// kernel/arch/x86_64/per_cpu.cpp
PerCpu& PerCpu::self() {
    return *base::PtrAt<PerCpu>(ReadMsr(msr::kGsBase));
}

void InstallKernelGs(unsigned int cpu_id) {
    base::safety::Check(cpu_id < kMaxCpus, "CPU index exceeds static per-CPU storage");
    auto& block         = g_percpu_blocks[cpu_id];
    block.cpu_id        = cpu_id;
    auto const kAddress = reinterpret_cast<unsigned long long>(&block);
    WriteMsr(msr::kGsBase, kAddress);
    WriteMsr(msr::kKernelGsBase, kAddress);
}
```

InstallKernelGs 是开机那一次的装配，上一段的代码装的就是它，注释把分工定了格，IA32_GS_BASE 的指针从此在每一个特权级都落在块上，再也不挪窝了，入口汇编寻址不需要任何交换的动作，IA32_KERNEL_GSBASE 的起点也在块上，之后行当改成了中转站，任务切换把每个任务自己的 GS 值从这里搬进搬出。所以上一节的 gs_base 字段读写的都是这个中转，接口就是 per_cpu.hpp 里的 StoreTaskAddressRegs 与 LoadTaskAddressRegs，调度器的 sink（注入进来的切换执行者）里有一根 swap_address_regs，它站的正是上面数过的前两个接口，把 outgoing（退场的任务）的基址存回了任务，把 incoming（进场的任务）的基址摆上了台面。WriteFsBase 与 WriteKernelGsBase 的两个直写口站在另一条路上，arch_prctl（用户态改线程基址的系统调用）一类的即时写走的是它们，写完的那一刻就生效，不经 swap_address_regs 的手，我们排基址问题的时候照上面划的分界查。

MSR 的编号住在 `msr.hpp`，文件头注释讲的事故值得一读：这些数字是架构的硬事实，推导的余地一点都没有，抄错了一位编译也照样通过，然后炸在很远的地方，注释自述 FS、GS 与 KERNEL_GS 三个编号曾经被挪错了一位，一次 swapgs 拉回来的就是一个零。所以编号的集中地只有 msr.hpp，命名学的是 Linux 的 msr-index.h，我们写代码的时候拿名字，不碰十六进制的字面量。同目录的 `instructions.hpp` 管读写的动作，ReadMsr 与 WriteMsr 包的是 rdmsr 与 wrmsr，注释给了一条汇编层的警告，wrmsr 只吃 EDX 与 EAX 各自的低 32 位，把一个 64 位寄存器整体塞了过去、指望高位活下来的写法，最早的那批内核就是这样跳到不相干的地址里去的，注释把这段记了下来。

## 中断向量与桩

向量地图的家在 `vector_config.hpp`，上面的数字不多，但是每一个都是几处共用的约定，我们数的时候要连注释一起读。kFirstIrqVector 定的是 0x20，十六根设备线占的是 0x20 到 0x2F 的地盘。kFirstMessageVector 定的是 0x40，消息中断的池子从这里起，座数由 `kernel/interrupt/irq_config.hpp` 的 kMessageVectorCount 定成 8，消息中断走的是 message-signaled 的路子，设备的投递不占中断线，改投的是一次内存写。kSpuriousVector 定的是 0xFF，收的是假中断（线没有真触发也冒上来的空响）。注释把挑 0x40 的理由写了出来，这个号段躲开了每一位常住户，设备线的桩占的 0x20 到 0x2F、本地时钟的 0x30、重调度 IPI（处理器间中断）的 0xE0、假中断的 0xFF，0x30 与 0xE0 的住址在 `smp_config.hpp`。注释的名单里还挂着 sigreturn 的 0x80，现行源码里找不到它的安装点，装门的代码只住在 isr.cpp 与 irq_stubs.cpp 两处，信号返回如今走的是 kRtSigreturn（15 号系统调用）的路。

门的机制归 `idt.hpp` 与 `idt.cpp` 这一对文件，全表的容量是 256 项。注释交代它的住处是 .bss 节，文件体积上的开销是零，认领了一段向量的主人自己填自己的那一段。每一项 GateEntry 是 16 字节的 packed 结构，EncodeGate 把一个处理函数的地址拆进项里去，而且它整个是 constexpr（编译期可算的），注释给的用意是让 host 世界的 test_idt 提前验一遍，把每个字段的拆法在碰硬件之前按住。门型只有 kTypeInterruptGate 的 0x8E 一种，含义是描述符有效位开着、特权级 0、32 位中断门三者的组合。InstallGate 的活是写一项，LoadIdt 的 lidt 让全表一次生效，注释还补了一句没装门的后果，CPU 会拿 #GP 把它拦了下来，错误码里带的就是向量号，咱们排查的时候顺着号就能把肇事的名字找出来。

桩的写法是这个文件族最有意思的取舍，`isr.hpp` 的头注释说得很清楚：桩由编译器从 x86 的 interrupt 属性生成、不走宏数组、也不走手写汇编，模板实例化把向量号烙进每一次的调用里。签名形状有带错误码与不带错误码的两种，最后汇进的是同一个报告函数。哪几个向量推错误码是 CPU 定的，`isr.cpp` 里的 kVectorPushesErrorCode 表把答案录成了布尔的一行，表里记真的号是 8、10 到 14、17、21、29、30，其余的都记假。C++ 收到的 InterruptFrame 只有 CPU 亲手推的五个字（rip、cs、rflags、rsp、ss），通用寄存器的保存是属性替我们做掉的事。

```cpp
// kernel/arch/x86_64/isr.cpp
template <unsigned int Vector>
__attribute__((interrupt)) void exception_no_code(InterruptFrame* frame) {
    cinux::arch::isr::ReportFault(Vector, *frame, 0);
}

template <unsigned int Vector>
__attribute__((interrupt)) void exception_with_code(InterruptFrame*    frame,
                                                    unsigned long long error_code) {
    cinux::arch::isr::ReportFault(Vector, *frame, error_code);
}
```

异常族占的地盘是 0 到 31，InstallExceptionStubs 一批就装完了。设备线族占的是 0x20 到 0x2F，irq_stubs.cpp 的 InstallIrqStubs 把它们也一批装完了，注释还交代了心跳的座位为什么填在这里，向量 0x20 是心跳属于 x86 的接线知识，所以它归架构层管。心跳链怎么走、Tick 怎么计数的细节，[PIT 定时器](drivers/pit.md)那一页已经把节奏讲完了，桩的统一形状这里只立一条：设备线、本地向量与消息向量三家的桩走的是同一个节奏，救好了浮点现场，进出的是 FpuGate，`kernel/proc/fpu_gate` 的进出守卫（进门的一拍存现场，出门的一拍恢复，嵌套的层数按任务记），帧交给了中断服务，出口过的是调度器的抢占检查，我们读桩的时候认这个节奏就够了。本地向量桩的走法另有一处不同，进门自己就 EOI 了本地控制器，计数与 on_timer_tick 的活都在桩内做完，不经过 `kernel/interrupt` 的 Irq 服务。整个 irq_stubs.cpp 住在无 SSE 的孤岛里，注释的理由是 iret 返回要路过的一切都不碰向量寄存器，向量寄存器说的就是 SSE 的浮点那组，孤岛纪律的来历 [PIT 定时器](drivers/pit.md) 也讲过了。

芯片侧的前后有两任后端，我们分开认。老的一任是 PIC（两枚 8259 中断控制器），pic.hpp 的注释说它的 remap 把 BIOS 惯用的 8 与 0x70 搬到 0x20 与 0x28，不搬的话芯片默认的向量就跟 CPU 的异常挤在同一个号段里，一来就串了线。两处细节的出处都在注释里，级连线 2 被顺带解开的原因是注释给的，从片的敲门要路过的是它。应答的讲究是从片的线两枚都要谢到，漏答一次这根线就哑了，tick 测试特意连收了三拍心跳才认，抓的就是这类一次性中断（test/kernel/test_tick.cpp）。新的一任走 InstallApicBackend，从 ACPI 的 MADT（固件交出来的中断拓扑表）找到本地与 IO 控制器，两枚分别做了 init，装上 0xFF 的假中断座并用它使能本地 APIC，之后 IO APIC 交给了中断服务，0x40 起 8 座的消息池注了进去，消息桩也装好了。消息中断的应答归本地 APIC，irq.hpp 的注释写明它从不落到线芯片头上，因为消息投递的终点就是本地控制器。

## 故障报告：ReportFault 的座次

ReportFault 是全部 CPU 异常唯一的落点，它的实现在 `exception.cpp`，我们把它的座次按顺序认一遍。进门救下的头一样东西是浮点现场，救完再进的是 FpuGate，它管的正是让被中断任务的浮点状态完整地跨过内核的入口，报告的路径也在被保护的一侧。缺页（向量 14）来了之后，resolver（装上来修缺页的口子，座位在下一站）的接手之前还有一道自查，出错的是内核态，地址没出用户上限的地界，CR4 的 SMAP 位开着（SMAP 是 CR4 的用户页访问禁位），rflags 的 AC 位没开（AC 是 rflags 里的放行开关，stac 与 clac 开关的就是它），四条都对上了就是 SMAP 违例，resolver 就被跳过了，后面的座次照走它的流程。

排在这个检查之后的是 resolver 座位，它兜的是能修好的缺页，`isr.hpp` 的注释把设计立场讲明了：内存世界的写时复制答案装在这里（写时复制，两个世界共享的是同一页、写入的那一刻才各自分开的机制，机制的全文在 [虚拟内存管理](mm/vmm.md) 里），座位只设了一个，这是注释替我们划下的界线，内核对缺页为什么可以活下来这件事只允许有一套说法。第二个口子留给的是异常修复表，管它的是 `ex_table.hpp` 与 `ex_table.cpp`：明知道可能踩空的内核指令（用户态拷贝最典型）每条栽一个 Entry，一条 Entry 的大小是十六字节，fault_from 记的是出事的指令地址，land_at 记的是接盘的落点。链接器按翻译单元的顺序摆表，启动的时候 ArmTable 排一次序，之后的查找全是二分，FindFixupFor 答不上的时候给的是零，把零当作未命中时的返回值是安全的，注释给的理由是合法的落点必然是内核文本、零永远轮不到。内核态的缺页走到这里被表接住了，rip 被改成了落点，iret 直接就回去了，拷贝的函数自己报告了失败。

再往下轮到的是 diagnoser 座位，resolver 已经摇头、故障正在走向转储的时候，它的活是打印第二意见。注释给它的边界划得很清楚，它打印的全是证据，碰不到的是控制流。第三个口子留给的是预约式恢复，ArmRecoverableFault 预约的是一个向量与一段字节数，真来了的话 rip 就往前拨这么多、再由 iret 送回去，TakeRecoveredVector 负责把这一次恢复的向量取走，注释的自评是它是真实内核异常表的缩影，伺候的是探测性访问。全都接不住的时候才轮到转储，我们看它最后做了什么：一张 32 个名字的表报的是异常叫什么，帧的前四个字和错误码也打了出来，缺页的路径多走一步，沿当时的 CR3 把 pml4e 到 pte 的四层逐层打出来，哪一层断了停的地方就是哪一层，末了 PanicDump 收场。转储链的内存侧走读，[虚拟内存管理](mm/vmm.md)那一页已经带我们走过了。

## 关中断与停车

中断开关的纪律住在 `irq_guard.hpp` 与 `irq_guard.cpp`，里面住的是一对函数加一个守卫，咱们看代码。SaveAndDisableIrq 的实现是 pushfq 加 cli，rflags 被整个快照了下来、紧接着关中断，RestoreIrq 做的是条件恢复，快照里的 IF 位开着才 sti。头注释把这套形状的用意讲白了，嵌套的守卫在别人已经关了中断的时候打开，退出的时候也要保持关着，经典的提前开中断事故在这个形状里根本写不出来，退出的凭据永远是拿进来的快照，无条件 sti 的写法在这里不存在。

```cpp
// kernel/arch/x86_64/irq_guard.cpp
unsigned long long SaveAndDisableIrq() {
    unsigned long long snapshot = 0;
    __asm__ volatile("pushfq\n\tpopq %0\n\tcli" : "=r"(snapshot) : : "memory");
    return snapshot;
}

void RestoreIrq(unsigned long long snapshot) {
    if ((snapshot & kIfBit) != 0) {
        __asm__ volatile("sti" : : : "memory");
    }
}
```

IrqGuard 拿的正是这对函数当内核，套进 ScopedGuard（仓库公用的守卫壳，进出作用域各调一次策略的钩子）的壳里，进域的时候快照，出域的时候条件恢复，一层一层地嵌套都不出事，你嵌多少层都不怕。停车的活有一个专门的复合动作 EnableIrqAndHalt，实现只有 sti 与 hlt 紧挨着的两条，注释把它的约定写了满满一段：sti 带一条指令的中断影子，hlt 的执行早于任何处理函数的进入，所以睡下去与开中断是原子的。把人叫醒的也可能是无关的中断，调用者的本分是循环重查等待的条件。注释里的警告是别拿 RestoreIrq 加一根单独的 hlt 替它，那一对会把原语本来要关掉的丢唤醒窗口重新打开。最底下的 Halt 是一个 hlt 死循环，halt.hpp 的注释说它是每个干净结局与每次早期失败的最后停靠点，要安静的话调用者自己把中断关好了再来。

## GDT 与 TSS

GDT（全局描述符表）的家在 `gdt.hpp` 与 `gdt.cpp`，我们把七项数完，打头的是空描述符，内核代码与内核数据结成的一对，TSS 的 16 字节双槽一对，收尾的是用户数据与用户代码。头注释把表的成长史记了下来，三槽的形状服务纯内核的年代，用户态上岗了之后，任务状态对与用户对才添了进来。用户对的选择子另有来历，注释讲的是 SYSRET 的算术，STAR（SYSRET 取内核与用户选择子的那颗 MSR）的基址里，带的就是请求特权级 3（RPL 3），写成的数是 0x23，SYSRETQ 落回用户态的时候 CPU 自己加 8 与 16，得到的就是 0x2B 与 0x33，不管 CPU 有没有替我们置 SS 的 RPL。static_assert 把 0x2B、0x33 与第 5、第 6 槽的归属按住了，描述符的编码是 constexpr 工厂，test_gdt 在 host 世界把每个字段的拆法再查一遍。

LoadOwnedGdt 是换表的动作，我们把次序跟着走一遍。块的份数是每核一份，装的时候把本核 Tss 的真实地址当场编进 TSS 槽，因为表里的 TSS 基址等的是运行期才知道的地址，MakeKernelGdt 给的静态值故意是零。之后的顺序是 lgdt、远返回换到 0x08、重装 DS、ES、SS，注释说明此时的中断还没开，重装的动作不跟任何东西抢时机。ltr 的归属在任务状态那一侧，段表这边就不管它了。

TSS 的家在 `tss.hpp` 与 `tss.cpp`，它的大小是 104 字节。头注释给它的定位很实在，长模式里的它不是硬件任务切换，调度的活从来不靠它，它挣到座位靠的是 rsp0 一个字段，ring 3 来中断时 CPU 要装的那根内核栈。ist 数组的每一格都留着零，注释的表态是中断栈表属于将来，眼下还没有默认的使用者。这批文档的旧版写过 IST1 指着一张独立的双故障栈，今天的源码里没有这件事，我们不写没有的东西。布局的偏移是架构自己的，注释说轮不到我们挑，rsp0、ist、io_bitmap_end 三个偏移的岗位各有断言按着，0x04、0x24、0x66 的数就是它们的岗位。Tss 的份数也是每核一份，self() 的取向是跟着 per-CPU 块走，切换的 sink 每次交接把 rsp0 对到 incoming 的栈顶，task_runtime.cpp 里写下的就是一行赋值。io_bitmap_end 被写成了 sizeof(Tss)，说明这个内核的 I/O 位图压根不存在，尾巴字段指到的地方就是位图的外头。

## 字级原语

`registers.hpp` 的头注释自称是 C++ 叫不出名字的几条指令的薄封装，注释里记的几笔，我们挑要紧的说。CR0 的写保护位它记了一笔，复位的时候它的值是零，ring 0 可以无声地改写只读映射，写时复制与一切写异或执行的计划都站在它的身上，所以内核一起步就把它打开了。写入的纪律注释也定了，调用方只翻带名字的位，重建整个字的事从来不干，因为机器拥有其余的位。CR4 的注释提了两件事，它的归属是单个处理器，读它答的是本核的答案。CPU 不认识的位会拒绝整个的写入并报 #GP。SMEP 与 SMAP 的开闸因此都要问过 CPUID，前一个说的是内核不许执行用户的页，后一个说的是内核不许碰用户的页。

问询的口在 `cpuid.hpp`，头注释说内核问的只有一类问题，这个 CR4 位实现了没有，因为 2010 年代的保护位全部住在 7 号叶里。HasSmep 与 HasSmap 问的就是这套，你去问的时候次序也一样，问的是 0 号叶够不够 7，够了再去问 7 号叶的位。

```cpp
// kernel/arch/x86_64/protections.cpp
void EnableCoreProtections() {
    unsigned long long cr4 = ReadCr4();
    if (HasSmep()) {
        cr4 |= kCr4Smep;
    }
    if (HasSmap()) {
        cr4 |= kCr4Smap;
    }
    WriteCr4(cr4);
}
```

开闸的动作就是上面的 EnableCoreProtections，它的套路是读一遍、或上答应过的位、再写回去，头注释补了多核的一半。AP 蹦床的活是把次级处理器拉起来上电，这段启动代码把引导核的 CR4 原样照搬，所以引导核开一次门，我们读到任何一个核的 CR4，看到的都是同一份答案。

`instructions.hpp` 收的是一句话的原语，我们一个一个过。CpuRelax 的本体是 pause，自旋等待的时候替流水线松一口气。Stac 与 Clac 开关 SMAP 的窗口，你在内核拷贝用户数据的代码里会碰到这一对，注释给的纪律最严，窗口必须在任何的阻塞或调度之前关上，因为 AC 位属于开它的那颗 CPU，一次切换的功夫就能把窗口掐断在拷贝的中途。每一条开窗的路径都要有对应的关窗，故障的路径也不例外，修复的动作是关了窗再报告拷贝的失败。没有 SMAP 的机器执行 stac 直接 #UD（无效指令），所以调用之前要看 CPUID 的脸色。CR3 与 TLB 的使用纪律，[虚拟内存管理](mm/vmm.md)那一页已经把谁改了谁刷讲清楚了，本页就不再复述了。

## 给上层暴露的接口

这一层的门面我们按消费者清点。调度与进程这一侧拿到的是切换的零件：ContextSwitch 与 TaskTrampoline 的两个汇编入口、CpuContext 的图像本身、InstallKernelGs、Store 与 LoadTaskAddressRegs 的一对基址交换口，还有 PerCpu::self() 给出的每核状态，current、slice_ticks、preempt_depth 的读取全走这里，[进程与调度器](proc/process_and_scheduler.md) 把 sink 怎么用它们讲过了。

中断侧的消费者要看的最少，门与桩装好了之后，驱动只跟 `kernel/interrupt` 的中断服务打交道，它手里握的是两张座位表，设备线的十六座一张，消息向量的八座一张，座位、开线、应答全是服务内部的活，irq.hpp 的头注释写明 EOI（中断的应答动作，芯片收到的就是它）从不出自任何驱动与桩的手，咱们写驱动的时候连碰都碰不到它，桩的出口统一过一遍抢占检查。内存这一侧拿到的东西要数缺页的座位与修复的表，缺页座位的两个名字是 resolver 与 diagnoser，ArmTable 与 FindFixupFor 管的是异常修复表，CR2 的读取也在这里，形状的验证由 IsCanonicalAddress 管，动手写基址 MSR 之前问它一句就够了。

系统调用入口这一侧拿到的是 per-CPU 块头上的四个四字（gs:0 到 gs:24），书写的权利全在入口汇编的手里，STAR、LSTAR、SFMASK 这些入口要碰的编号也一样，住的地方只有 msr.hpp。字级的原语谁都用得上，不过每一处的语义都写在文件的注释里，我们记下的约束一共三条，翻位不重建、动 MSR 之前验形状、SMAP 的窗口开了要关。

## 源码位置

- `kernel/arch/x86_64/context.hpp`、`context_switch.S` — CpuContext、FpuImage、ContextSwitch、TaskTrampoline
- `kernel/arch/x86_64/per_cpu.hpp`、`per_cpu.cpp`、`per_cpu_config.hpp` — per-CPU 块、GS 基址纪律、kMaxCpus
- `kernel/arch/x86_64/msr.hpp`、`instructions.hpp` — MSR 编号、rdmsr/wrmsr、pause、stac/clac
- `kernel/arch/x86_64/registers.hpp`、`cpuid.hpp`、`protections.hpp`、`protections.cpp` — 控制寄存器、CPUID 问询、SMEP/SMAP 开闸
- `kernel/arch/x86_64/vector_config.hpp`、`smp_config.hpp` — 向量分配与本地向量编号
- `kernel/arch/x86_64/idt.hpp`、`idt.cpp`、`isr.hpp`、`isr.cpp` — 门表、门编码、属性桩、异常族安装
- `kernel/arch/x86_64/irq_stubs.hpp`、`irq_stubs.cpp` — 设备线、本地向量、消息向量、假中断的桩与后端安装
- `kernel/arch/x86_64/pic.hpp`、`pic.cpp` — 8259 双芯片的重映射、开线、应答
- `kernel/arch/x86_64/exception.cpp`、`ex_table.hpp`、`ex_table.cpp` — ReportFault 座次、resolver/diagnoser 座位、异常修复表
- `kernel/arch/x86_64/irq_guard.hpp`、`irq_guard.cpp`、`halt.hpp` — 关中断守卫、sti 加 hlt 的停车、最终停靠
- `kernel/arch/x86_64/gdt.hpp`、`gdt.cpp`、`tss.hpp`、`tss.cpp` — 段表、SYSRET 选择子算术、任务状态段
- `kernel/interrupt/irq.hpp`、`irq_config.hpp` — 中断服务的两张座位表与消息池容量
- `test/unit/test_context.cpp`、`test_idt.cpp`、`test_gdt.cpp`、`test_extable.cpp`、`test_cpu_bits.cpp` — host 世界对图像、门、描述符、修复表的复核
