# PIT 定时器

## 功能概述

这一页我们把 PIT（8254 可编程间隔定时器，x86 板上年纪最大的一代时钟芯片）在时钟体系里担的差事走一遍。它今天的差事分两路，芯片的三路计数通道，今天用上的是 0 路与 2 路。通道 0 走的是 100 Hz 的方波，IRQ0 那根线上的每一次跳动，都由 Tick 服务记下了一拍，随后把该不该换人的问题报给了调度器。通道 2 管的是微秒级的短延迟，PS/2 启动的每一次等待与多核拉起备用处理器的每一个间隔，走的都是它。HPET（高精度事件定时器）站的则是另一个岗，它的活只有自由计数、不接中断，给时钟系统调用供两次节拍之间的细读。我们顺着芯片的编程方式、通道 2 上的微秒延迟、Tick 服务的类型擦除、中断链上的心跳、本地 APIC 时钟的校准一路走过去，HPET 的分工放在最后画清。

## 芯片的编程方式

Pit 类的家在 `kernel/driver/pit/pit.hpp` 与 `pit.cpp`，它的形状是 Meyers 单例，构造发生在头一回取用的时候，跟 Pmm（物理内存管理器）与 Tick 用的是同一个模子。头文件的注释把这层分工写得直白，芯片把全部状态扛在了自己的身上，管通道 0 的节拍时，内核要会的东西只有三个旋钮，也就是命令字、除数的低字节、除数的高字节。露在外面的脸是 start 与 wait_microseconds 两张，脸之下的输入时钟、端口地址、除数算术全是器件事实，pit.hpp 的文件头注释说它们住进了 .cpp，消费的一方一概看不见。1.193182 MHz 的输入时钟就是其中一件，装它的 kPitInputHz 与另外三个常量，也就是两个端口的地址与一个命令字，一起待在 pit.cpp 的匿名命名空间里，我们把器件与服务旋钮的分界记在这句注释上，后面 Tick 一节我们还会回到它。

编程的动作只有一个口子 start(Hertz)，start 的写法我们顺着表里的次序读。除数由 1.193182 MHz 除以请求的频率得出，商必须装得进 16 位的计数器。三笔端口写排的是同一张表，OutB 一口气把表交了出去，写入的次序就是表里的次序。0x36 这个命令字进的是 0x43 的命令口，它对芯片一次说定的是这几件事，通道取的是 0，装数走的是低字节在前、高字节在后，模式取的是方波，计数用的是二进制。跟着除数的低字节与高字节挨个进了 0x40 的数据口，方波一起振了起来，通道 0 的 OUT 线就按除数定下的周期翻面，中断的来源正是这根线。

```cpp
// kernel/driver/pit/pit.cpp
void Pit::start(cinux::base::Hertz rate) {
    const auto kDivisor = static_cast<unsigned short>(kPitInputHz / rate.value);

    const cinux::driver::PortWrite kWrites[] = {
        {.port = kCommandPort, .value = kSquareWaveCommand},
        {.port = kChannel0DataPort, .value = static_cast<uint8_t>(kDivisor & 0xFFU)},
        {.port = kChannel0DataPort, .value = static_cast<uint8_t>(kDivisor >> 8U)},
    };
    OutB(kWrites);
}
```

## 通道 2 上的微秒延迟

`wait_microseconds(base::time::Microseconds span)` 是驱动的第二个口子，头文件给它的说明是拿通道 2 做等待、不去碰通道 0 的节拍，传进去的参数得非零，上限给的是五万微秒，Check 会当场拦下越界的请求（Check 是这个内核的运行时断言，命中了就停机报告）。上限的五万这个数，头文件只给了它、没有给理由，我们把顶摆在这里：十六位的计数器装到 65535 格，按 1.193182 MHz 的输入时钟折算，溢出的临界在约五万四千九百微秒，五万收在临界线的下面，余量留得足足的。

数格数的动作我们分开认。进门的第一笔写把 0x61 的 bit 0 与 bit 1 都清了下去，闸门与扬声器的两条线都断了电，0x61 这个口是闸门与扬声器合用的。命令字这里换的是 0xB0，它对芯片说的是通道 2、装数照旧低字节在前、模式取单发，数完之后 OUT 就竖起来了，不重新编程的话不会再触发。延迟的格数按 1.193182 MHz 与微秒数折出来，取整取的是向上，等的时长因此不会短于请求，低高两字节写进的是 0x42 的数据口。格数装好之后的动作是开闸，bit 0 通的是通道 2 的闸，bit 1 通的是扬声器，通道 2 数完的信号从第 5 位读得回来。等的姿势是忙等，我们在第 5 位上打转，每一次打转之间垫的是一发 CpuRelax，位竖起来了就是数完了。出门之前 0x61 的原值放了回去，配的正是进门那笔清位，扬声器口的原始状态不归这段代码处置。

这一段延迟今天喂着的头一家是 PS/2，那一层的接缝 Ps2Ports 有一个 delay 槽，host 的实现把它接到了 `Pit::self().wait_microseconds(span)` 上，键盘与鼠标启动时的每一步等待（它们与设备问答的次序，[键盘驱动](keyboard.md)里我们认过）走的就是它。多核那边的用处更多，备用处理器的时钟校准拿它垫上整整 10 毫秒，备用处理器复位与起跑用的 INIT、SIPI 两发处理器间中断，中间的间隔也归它掐着。

## Tick：类型擦除的节拍服务

Tick 数的是开机以来的心跳，家安在了 `kernel/time/tick.hpp`，形状沿用的还是 Meyers 单例，常量初始化的做法保住了零构造的规则。它对后端的要求只有一句话，我们把这段接缝搬来：

```cpp
// kernel/time/tick.hpp
template <typename Backend>
concept TickSource = requires(Backend& backend, cinux::base::Hertz rate) {
    { backend.start(rate) } -> std::same_as<void>;
};
```

后端只要会按给定的频率把振荡开起来，别的本领一概不问，我们对着 concept 里的那一行就能数清。tick.hpp 的注释把这层刻意的无知写明了，需求等到消费的那一天再加，今天的清单就一条。类型擦除的现场在 init 里，来的设备只要满足上面这个概念，init 就把一个无捕获的 lambda 收成了 start_ 这个函数指针，设备的指针跟着当上下文，此后调用面见到的只是这对组合，设备本来的名字谁也看不见。类型擦除（把具体的类型藏进一对函数指针与上下文指针）这个手法是本页头一回提，后面的中断服务用的也是同一个路数。注释把用意也写明了，消费者认的是一张稳定的脸，哪天芯片换了、连架构换了，消费的代码一行都不用动。

频率的旋钮 kTickHz 住在 `kernel/time/tick_config.hpp`，我们认到的值是 100_Hz。tick_config.hpp 的文件头注释把分居的道理交代了，服务的旋钮进 config 文件，器件的事实留在驱动里，调系统的动作因此永远是动一个常量、碰不到类的脸。

init 的动作一气呵成，我们把它的两拍分开数。注册会把计数清了零，跟着按 kTickHz 把设备开了起来，注释对早开的态度很宽，中断的总闸还关着的时候，设备振得再早也是白振的，这样的早开是无害的。这一点我们拿测试守着，`test/kernel/test_tick.cpp` 的两个用例，一个等的是三拍到位，另一个在闸关着的时候空转一圈、计数一格都不动，两头的说法都有实证。

数拍的口 on_interrupt 我们也要看一眼，它的实现只有一行 fetch_add，内存序取的是 relaxed。inline 的缘由注释交代了，它要在调用者那座无 SSE 的中断孤岛（整条路径不碰向量寄存器）里实例化，进门时中断已经是关着的，所以路径越短越好。计数器上的 static_assert 我们也数到了，atomic<unsigned long long> 必须是无锁的，注释的立场是 IRQ 的计数器不许调带锁的运行时。since_boot() 把计数读了出去，服务开张之前答的是零。

引导序列里的站位我们对着 init_sequence.cpp 认一遍。tick 这一步的一句话是 `time::Tick::self().init(driver::Pit::self())`，位置排在 irq stubs 与 sti 的前面，tick 步走完了，PIT 已经在振了，但中断的闸还是关着的，谁也听不见它的动静，装到 irq line 0 的那一步线才开，sti 那一步中断才真正放了进来。内核侧的测试 test_main.cpp 也用同一句话把 Tick 接到了 Pit 上。

## 中断链上的心跳

heartbeat 是 IRQ0 的座位，InstallIrqStubs 把它坐上了 0 号线，我们把它的全身搬来：

```cpp
// kernel/arch/x86_64/irq_stubs.cpp
void heartbeat() {
    cinux::time::Tick::self().on_interrupt();
    cinux::proc::Scheduler::self().on_timer_tick(cinux::syscall::MonotonicNanoseconds());
}
```

线上来的这一路我们跟着走一遍。模板烙出来的 irq_entry 把浮点现场救好，把帧交给了 Irq::dispatch，dispatch 把 handler 跑完了，又替它应答了芯片，irq.hpp 的注释写明应答完全是中断服务的内部活，任何驱动与桩的手里都不出 EOI。收芯片的接缝 Irq 也是同一对组合，init 把 unmask_ 与 ack_ 两枚 lambda 收成了函数指针，芯片的指针照旧跟着当上下文，irq.hpp 的 init 注释把这层来历交代了，Tick 一节埋的那句话在这里兑了现。中断的出口处还要再过一遍调度器的抢占检查，换人的那一段故事在[进程与调度器](../proc/process_and_scheduler.md)里讲过了，本页就不重复了。

heartbeat 身上的两件事我们分着看。一件是 Tick::on_interrupt 把这一拍记下了，另一件是把 MonotonicNanoseconds() 报给了 Scheduler::on_timer_tick，调度器拿这个时刻查 real timer（进程的 alarm 一类到点发信号的定时器）到期没有，再问当前任务的类要不要换人，它做的只有记录、没有切换。thunk 住在 irq_stubs.cpp 的缘由，文件头的注释交代了，整个文件属于无 SSE 的孤岛，iret 返回要路过的一切都不碰向量寄存器，on_interrupt 在 tick.hpp 里写成了 inline，伺候的也是同一个孤岛。

## 本地 APIC 时钟：PIT 量出来的节拍

每个 CPU 的本地 APIC（每个处理器自带的中断控制器）带一个往下数的时钟，内核把它接成了第二个节拍源，走的向量是 kTimerVector（0x30，smp_config.hpp 定的数）。它的周期没有人手填，是拿 PIT 当尺子现场量出来的，校准住在 smp.cpp 的 BootSecondaryCpus 里，我们把量法的四行搬来：

```cpp
// kernel/arch/x86_64/smp.cpp
local.start_timer(kTimerVector, kInitialCounter, false);
driver::Pit::self().wait_microseconds(kCalibration);
g_timer_count = kInitialCounter - local.timer_remaining();
local.stop_timer();
```

校准我们一句句过。本地时钟的计数器从 kInitialCounter（32 位全 1）往下数，start_timer 的第三参是假，这一发走的是单发，分频取的是 16，分的是计时器自己的输入时钟，设置落在 local_apic 的分频寄存器里。PIT 的通道 2 替它垫了整整 10 毫秒，也就是 kCalibration 的值，10 毫秒里本地时钟数掉了多少格，一个周期就定成了多少格，量出来的数落在了 g_timer_count 里。之后的事交给 ApMain，每个备用处理器拿 g_timer_count 按周期模式起了表，每个核就有一台自己的 100 Hz 节拍器了，不再挤 PIT 那根共享的线。量出来的格数是零的话，BootSecondaryCpus 答的是假，多核起不来了，主核就独自继续了。

本地向量这一路的出口是 local_entry，与设备线的走法有一处不一样，我们也把它认一遍。它进门就 EOI 了本地控制器，来的向量是 kTimerVector 的话，per-CPU 的 local_timer_interrupts 计一次，跟着把 `on_timer_tick(MonotonicNanoseconds())` 也调了一遍，其余的本地向量归 reschedule_interrupts 计数。这个 per-CPU 的计数是每个核自己的，PIT 的 heartbeat 不碰它，两条路就在 on_timer_tick 里汇合了。

## HPET：不响铃的自由计数器

HPET 的家在 `kernel/driver/hpet/`，头文件给它的定位是钟面，它的活只有自由计数，它与 PIT 的分界我们就在此处画清。init 从 ACPI 的表里找 HPET，把它的 MMIO 窗口映射了进来，读能力寄存器核对的是两件事，计数器得是 64 位的，报出来的周期值得信，然后才把主计数器开了起来。有一处取舍 hpet.cpp 的注释写得明白，init 把 legacy 路由的那个位按了下去，IRQ0 与 IRQ8 的归属仍是 PIT 与 RTC，比较器（HPET 里能到点拉中断的定时单元）的中断使能位也没有动，这个驱动从不做武装比较器的事，它要的只是计数器自己在跑。

since_boot_ns() 的读法我们也要认一下，头文件的注释说底数取的是 init 那一刻的读数，当前计数减出来的差按飞秒（一飞秒是百万分之一纳秒）的周期折成了纳秒，两次读数的相减因此不管 init 跑得多晚都还是干净的。它最大的客户是 MonotonicNanoseconds()，sys_clock.cpp 里的分支很短，HPET 在就报 HPET 的纳秒，不在就退回 Tick 的拍数乘一千万，也就是 100 Hz 一拍 10 毫秒的粒度。init_sequence 里 HPET 缺席时打出的那行 `hpet absent, clocks stay tick-grained`，说的正是这件事。NVMe 的超时忙等读的是 HPET 的纳秒，事件流的戳与 [AHCI 与它脚下的 PCI 总线](ahci_pci.md) 的预算读的是 Tick 的拍，sys_getrandom 把 Tick 的拍数掺进了熵源，两路时钟的分工就是各喂各的客户。

## 源码位置

- `kernel/driver/pit/pit.hpp`、`pit.cpp` — Pit 单例、start 的三笔编程、wait_microseconds 的通道 2 忙等
- `kernel/time/tick.hpp`、`tick_config.hpp` — TickSource 概念、类型擦除的 init、无锁计数、kTickHz
- `kernel/init/init_sequence.cpp` — tick、irq stubs、apic interrupt delivery、irq line 0、interrupts、hpet 各步的站位
- `kernel/arch/x86_64/irq_stubs.cpp` — heartbeat 的座位、irq_entry 与 local_entry 的出口、无 SSE 孤岛
- `kernel/interrupt/irq.hpp`、`irq.cpp` — 线的座位与开门、dispatch 内部应答
- `kernel/driver/apic/local_apic.hpp`、`local_apic.cpp` — start_timer、timer_remaining、stop_timer、EOI
- `kernel/arch/x86_64/smp.cpp`、`smp_config.hpp` — 校准的四行、ApMain 的周期起表、kTimerVector
- `kernel/driver/hpet/hpet.hpp`、`hpet.cpp`、`hpet_layout.hpp` — 钟面定位、init 的核对与开关、纳秒折算
- `kernel/syscall/sys_clock.cpp` — MonotonicNanoseconds 的两条分支
- `kernel/driver/ps2/controller_host.cpp` — host_delay 到 wait_microseconds 的接线
- `test/kernel/test_tick.cpp`、`test_main.cpp` — 三拍到位、关闸无声两个用例与测试侧的接线
