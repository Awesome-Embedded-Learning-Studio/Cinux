# 同步原语

## 功能概述

这一页咱们把 `kernel/proc` 里的锁族完整地走一遍。sync.hpp 的文件头给它的定位是“一把自旋、两路睡眠”：Spinlock 走的是快车道，中断关掉之后跑一条 test-and-set 加 pause 的循环，Mutex、Semaphore 和 Completion 都骑在它的身上，它们拿 Spinlock 看住自己的元数据，真要等的时候就放掉自旋锁、把自己停进一条侵入式的等待链。停车走的是调度器的 prepare_to_wait 加 schedule_blocked 两步接口，锁的所有权移交正好落在两步之间，醒来的人手里已经握着锁了，省掉了再抢一遍的功夫。这一组锁的设计是可观察的，sync.hpp 的注释管这个叫 `observable by design`：自旋锁记着它的持有人，解锁的时候要核对放锁的是谁，单核重入逃不过 Check 的眼睛，拿着自旋锁睡觉不会让机器悄悄地绞死，Check 会响亮地当场停下。它背后的登记表是 SpinLedger，它是防死锁防线的地基，咱们在它自己的小节里展开。给用户态的锁字（用户态存着锁状态的那个字）准备的是 futex，咱们也会把它的系统调用接口看一遍。至于锁怎么跟调度器配合，[进程与调度器](process_and_scheduler.md)的阻塞一节给过梗概，这一页把完整的机制讲完。

## Spinlock：快车道

Spinlock 的数据成员有六个，六个成员咱们在 `kernel/proc/sync.hpp` 里从上往下数：held_ 是一张 atomic_flag 型的旗子，owner_cpu_ 记着拿锁的核，irq_state_ 存的是进锁那一刻截下的中断状态，holder_ 记着持锁的任务，ledger_ 指的是登记表，name_ 是注册进去的名字。咱们看锁的源码，看的其实就是这些字段怎么被读写。

拿锁的入口是 `Spinlock::lock()`，`kernel/proc/sync.cpp` 里的顺序是这么排的。进门的第一步是 SaveAndDisableIrq，它把 rflags 的快照截下来存好，顺手把中断也关掉了，所以持锁期间本核既没有别的任务在跑，也挡住了所有想插进来的中断。接着咱们核对 owner_cpu_ 是不是已经等于本核的编号，两边的号码相等就是单核的重入，Check 就当场把机器停下来了，报错写的是 `spinlock re-entered on one core`。它的检查防的是咱们自己：中断关着的时候去 test-and-set 一面已经竖起的旗子，那就永远转不出来了，与其落得个机器转死的下场，不如在重入的那一刻就把问题讲明白。真正的竞争逻辑只有一行，`held_.test_and_set(std::memory_order_acquire)`，抢不到的时候就 `CpuRelax()` 转圈，x86 的 pause 指令让自旋循环在流水线里少占地方。锁到手了之后，irq_state_ 存下刚才截的中断状态，owner_cpu_ 记上本核的编号，然后向登记表报上这一次的 acquire，最后 holder_ 记下调度器的 current。

放锁的 `Spinlock::unlock()` 把顺序反过来走。头一件事是核对 holder_ 与当前的任务对不对得上，面孔不对的话 Check 就停机，报出来的信息是 `spinlock released by a non-holder`，holder_name() 这个口子留给咱们查持有人，任务之外的地方它答的是 irq/main。核对这一关过了，holder_ 的记录清空了，release 也向登记表报了，owner_cpu_ 的值归回 -1，held_.clear 用的就是 release 语义，最后 RestoreIrq 把中断恢复到 lock 截下的那个状态。咱们要注意恢复是按快照来的，就算拿锁之前中断本来就是关着的，放锁之后它们也照样是关着的，其中的道理咱们在中断保护的小节里细说。

RAII 的壳有两个入口，其中的一个入口是 `guard()`，它给咱们返回一个 SpinGuard，构造的时候拿锁、析构的时候放锁。retire() 是给临时锁准备的注销口，它在 IrqGuard 的保护下确认锁没有人拿着。IrqGuard 是把开关中断这对指令包成自动配对的壳，纪律咱们在中断保护的小节里细说。retire 然后叫登记表的 forget 把这个地址的身份抹掉。地址复用之前不清身份的话，旧记录就会冤枉新搬来的锁，常驻的锁不需要这一步。

## SpinLedger：登记表与防死锁的防线

SpinLedger 回答的是一个问题，问的是这个核手里还有几把没放的自旋锁。回答问题的是一张计数的表，咱们把每个核的计数分槽记下，它用的是 `depth_[arch::per_cpu::kMaxCpus]`，每个核一个自己的计数槽，depth() 读的就是本核自己的那个槽位。note_acquire 在 lockdep 的检查过了之后把计数加一，note_release 的 Check 查的是计数下溢，负着放说明登记表自己烂了，报出来的信息是 `spin ledger underflow`。

它为什么是一个实例类而不散成几个函数，sync.hpp 的注释给出了理由：测试的世界可以喂给锁一张自己的登记表，调度器的停车许可检查读的却是共享的默认表，两边的世界互不打扰。锁的构造参数 ledger_ 缺省给 nullptr，锁在每次 acquire 和 release 的时候落到 `DefaultSpinLedger()` 的身上，这个默认实例是 sync.cpp 里一个 static constinit 的单例，走的是零构造。于是调度器那边认的只有一句话，`DefaultSpinLedger().depth() == 0` 过了才许停车。咱们把它的四个入口数过了：block_current、schedule_blocked、wait_child 的停车点、stop_current，它们查的都是同一句 Check，三处报的信息统一，写的是 `task parked while holding a spinlock`，stop_current 的报错换了个词，写的是 stopped。拿着自旋锁去睡觉的话，等待链上的任务没有机会放锁，别的核的等待就没有尽头了，这就是真的死锁。这里的取舍是宁可当场响亮地炸，也不让机器悄悄地绞死，sync.hpp 的注释就是这么表态的。

计数只是登记表的一半，另一半是它转发出去的 LockOrder，`kernel/proc/lockdep.hpp` 里的实例类，管的是锁与锁之间的顺序。它的身份表是锁地址的槽表，容量是 64 个槽（代码里的 kLocks），每个核的 held 链是一条静态栈，深度的上限是 16（代码里的 kDepth）。acquire 的时候，它把新锁跟当前手里每把已持的锁连一条有向边，然后问图上的问题：新锁能不能反过来够到已持的锁，够得到的话就是成环，成环的答案叫 kCycle，新锁已经在 held 链里的话就是递归，递归的答案叫 kRecursive，满了的答案叫 kCapacity。这三样没有一样是善意的警告，sync.cpp 的模板函数 record_acquire 拿 Check 兜底，命中了就停机，报的错是 `lockdep acquisition failed: cycle, recursion or capacity`。release 只许放栈顶的那一把，放中间的答案叫 kOutOfOrder，拦它的是同一句 Check，报的错是 `not held or out of order`。forget 要求锁不在任何核的 held 链上才许抹身份，带着锁注销的话，报的错是 `lockdep destroyed held lock`。所以咱们在内核里写锁的代码，拿到的保证是实打实的，只要拿锁放锁的路径都走这组接口，A 之后拿 B 的路径第一次出现，图上就添了 A 到 B 的边，哪天有人从 B 起手去拿 A 的话，Check 当场把两条路径的持有链打印出来。dump 走的是 InstallLockdepSink 装进去的打印桥，打印出来的一行对应一把锁，带着 set_name 注册进去的名字。这套检查不在的时候也不占地方，checks_config.hpp 里的 kLockdepEnabled 是生成的配置，当前的构建把它设成了 false，转发目标就换成空操作的 InactiveOrder，眼下生效的就是它，调用方的代码一行都不用改。

set_name 的调用要赶在头一次拿锁之前，名字的存储归调用者保管，dump 打印的就是它。futex 那边给表锁注册的名字叫 futex table，咱们到 futex 的小节再回头看它。

## 两步停车：prepare_to_wait 与 schedule_blocked

调度器把阻塞分成了两步，这一组锁走的都是这两步，所以它值得单独的小节。`kernel/proc/scheduler.hpp` 给两个接口写的注释把分工定了：prepare_to_wait 在等待锁还握着的时候把当前任务标成 kBlocked，这个接口被调到的时候，调用者的任务已经挂上了等待链，schedule_blocked 接的是真正的切换，注释还补了一句，拿着自旋锁跨过这个调用会让机器响亮地停下，两步接口的空档让放锁赶在切换前面，Check 就落在了 schedule_blocked 的门口，这句 Check 咱们在上一节已经见过了。

咱们把两步为什么要分成两半讲清楚。prepare_to_wait 的实现拿 QueueGuard 进队列锁，把 current 的 state 写成 kBlocked，它要做的就这么多，切换不是它的活。schedule_blocked 进门开的是 IrqGuard，NoRescheduleGuard 没有压着的话，查的还是那句停车许可，然后的事情是挑接班人、换 current、交出 CPU，行里没人就走 return_to_main 回到 Main 的身边，Main 是内核的引导线程，[进程与调度器](process_and_scheduler.md)的调度器骨架一节里有它的 idle 循环。分两半的理由在两步的空档里：锁还在调用者手里的时候，状态就已经是停着的了，而切换要等锁放掉之后才发生。假如这中间别的核把锁放掉、顺着等待链把你看成了 kBlocked 去唤醒，unblock 看到的就是停着的你，它把你送回就绪行、标成 kReady，等你真正执行到 schedule_blocked 的切换点时，你已经是就绪的了，切走之后的人立刻能被挑回来，这一记的唤醒就没有丢。假如反过来把标状态和切换做成原子的一步，锁放掉与切换完成之间进来的唤醒就得靠额外的重查逻辑去追，两步分开走的话，唤醒碰到的是已经标好的 kBlocked，unblock 不用做额外的重查，也不用谁来补记一下了。

调度器还留了一个便捷的口子 block_current，它调的是 park_current，park_current 的活是把两步依次调一遍，两步在同一个调用里就做完了。它适合没有等待锁要放的场合，wait_child 只复用两步中的第二步并自带检查，它自己在队列锁的保护下把任务 push 进 waiters_，park_current 的口子它不走。锁族走的都是两步的接口，理由咱们拿 Mutex 的代码看。

所有权移交是锁族跟这两步配合的第二个要点，unblock 的注释在 scheduler.hpp 里写明，等它被调到的时候，锁就已经移交给了被唤醒的任务，咱们读这段代码时可以认定，醒来的人恢复执行时手里已经握着它等的东西。落到 unlock 的代码上，移交的顺序是摘队头、把 owner_ 写成他的指针、然后才 unblock，这三件事全都在自旋锁的里面做完。于是被唤醒的人恢复执行之后不需要再去 test-and-set 一遍，也不存在几十个被唤醒的人一起扑向一把锁重赛的场景，每一轮的竞态只有拿锁和入链的那一下，重赛这样的场景从结构上就不存在。

等待链是侵入式的，底细在节点的归属上。链节点 sync_wait_link 长在 Task 的身上。Mutex、Semaphore、Completion 各有各的 waiters_，挂的都是同一个节点的 SelfList，咱们挂链的时候动的是现成的节点，停车的时候不用分配内存，链也断不了，代价是一个任务身上只有一个这样的节点，同一时刻它只能停在这一组同步原语的一条链上。futex 用的是另一个节点 futex_wait_link，两条链的走向互不相干。

## Mutex：睡着的互斥

Mutex 的字段是三件套，三样东西咱们已经在 Spinlock 的小节里见过两个：看门的 Spinlock，指着持有者的 owner_，新来的只有自己的 waiters_ 等待链。拿锁的 `Mutex::lock()` 值得整段看，它在 sync.cpp 里的形状是这样的：

```cpp
void Mutex::lock() {
    // Release the spinlock before switching, but keep IRQs masked until
    // park_current() has committed the task's blocked state.
    const cinux::arch::IrqGuard kParkGuard;
    Task* const                 kSelf = Scheduler::self().current();
    {
        const SpinGuard kGuard(spin_);
        if (owner_ == nullptr) {
            owner_ = kSelf;
            return;
        }
        waiters_.push_back(*kSelf);
        Scheduler::self().prepare_to_wait();
    }
    Scheduler::self().schedule_blocked();
}
```

这段代码把本页前面几节的机关全部串在了一起，咱们一层一层读。IrqGuard 罩住了整个停车过程，sync.cpp 的注释给的交代是：切换之前要放掉的是自旋锁，中断的屏蔽却要撑到停车状态提交完。内层的 SpinGuard 里做的事分两种命运，owner_ 空着的话就直接认领走人，锁被人拿着的话就把自己 push 进等待链、标成 kBlocked，然后内层的作用域结束，SpinGuard 的析构把自旋锁放掉。走到这一步的时候，自旋锁已经不在手里了，调度器那句停车许可的检查自然通过，schedule_blocked 才放心地切换。这就是两步接口存在的直接理由，如果调度器只有 block_current 那个合一的口，拿锁的任务就得带着自旋锁进去，Check 会当场把机器停下来的。中断屏蔽撑到切换完成也有它的用处，别的核放锁的那一下要在自旋锁里改 owner_ 并 unblock，本核要是中途被中断搅进来的话，这一段元数据的读写就多出一层保护。

放锁的 `Mutex::unlock()` 是移交的那一半。进门的是 SpinGuard，Check 核对 owner_ 是不是当前的任务，为的是不让人替别人放锁，然后 pop_head 摘等待链的队头。摘到了人就照上一节说的顺序移交，把 owner_ 写成他的指针，unblock 把他送回就绪的行里，函数就直接返回了，锁没有囤积的说法。没摘到人的话 owner_ 置回空，锁回到无主的状态。try_lock 在这一版里没有它的位子，现在的 sync.hpp 里，Mutex 只剩四个接口了，四个的名字是 lock、unlock、guard 和 retire。想非阻塞试探的场合，咱们得用 Semaphore 的 try_wait，或者拿自己的原子量来做。

retire() 核对 owner_ 为空而且等待链空了才许注销，带着人注销的话，报的错是 `retired busy mutex`，然后顺着放掉里面那把自旋锁的身份。咱们认得出它跟 Spinlock 的 retire 是同一个路数。

## Semaphore：数单元用的计数器

Semaphore 的形状和 Mutex 差不多，看门的 spin_、等待链 waiters_，owner_ 换成了一个 long long 的 count_。字段只换掉了一个，咱们数的对象也从互斥换成了资源的个数，count_ 就是手里还剩的单元数。

wait() 扮的是 P 操作，咱们把它的流程跟一遍。IrqGuard 罩住的是全场，SpinGuard 里看的是 count_，大于零的话就减掉一个单元走人，等于零的话就进等待链、标成 kBlocked，放掉自旋锁之后的切换交给 schedule_blocked。try_wait() 是非阻塞的版本，count_ 为零的话就直接答 false，答 true 的场合已经扣掉了单元。post() 扮的是 V 操作，队头摘到了人的话就唤醒他，单元直接交到他的手里，count_ 就不动了。sync.hpp 的注释管这个叫 `unit transfers, no count bounce`，意思是单元从持有者过户给等待的人，不走加了再减的两跳，也就不存在中间状态被人拿走的问题。没摘到人的话，单元存进 count_ 留给将来的 wait。

retire() 查等待链空了才放行，咱们查的也是人走得干净没有，报出来的错是 `retired semaphore still has waiters`。

## Completion：一次性的汇合点

Semaphore 的单元是可以攒的，一次性的完成怎么办，咱们把它交给 Completion 来接。它的状态机有三态，三态的名字是 kPending、kSuccess、kFailed，字段是看门的 spin_、state_ 和一条 waiters_。它跟 Semaphore 的区别，sync.hpp 的注释讲得很明白：finish() 推的是 Pending 到终态，而且只此一次，之后的调用什么也不改，多出来的一场意外事件不可能像多出来的信号量单元那样，让下一个等待的人提前离场。

干活的地方是自旋锁里的 `finish(bool success)`，咱们把它的判断跟一遍。state_ 已经不是 Pending 的话就答 false，说明这个调用没有做成转换的事，转换成了的话就把终态写进去，摘下队头唤醒等着的人，答 true 表示转换是这一次完成的。wait() 是等的那一侧，SpinGuard 里看到终态的话就直接返回 success 与否，还是 Pending 的话就进链、标 kBlocked，等内层的 SpinGuard 放锁，剩下的切换交给 schedule_blocked。醒来的它重新拿 SpinGuard 再读一遍 state_，返回值就以这一次的读取为准，也就是说就算存在来源不明的虚假唤醒，答出来的还是状态机的真话。不想睡的上下文有两个只读的口，finished() 答的是终态到了没有，success() 的读要等 finished 为真之后，中断上下文里就该用它们而不是 wait。rearm() 的活是把终态拨回 Pending，给回收槽位的请求池用，等待的人还停着的话 Check 会拦下来，报的错是 `rearm with a waiter still parked`。retire() 同样要求链是空的。

## futex：给用户态的锁字

用户态的线程要互斥，咱们不能把内核的 Mutex 递出去，futex 的思路是把无竞争的路径整个留在用户态，分胜负靠的是一个字的原子操作，这是 futex 机制的通用设计，只有真的起了争用、真的要睡的时候才进内核。内核这一侧管的是两件事，头一件的活是把任务停在这个字上，另一件是把停着的任务叫起来。

系统调用的入口是 202 号，咱们把三个参数认一认。`kernel/syscall/syscall.hpp` 里 kFutex 的注释把形状写成了 `futex(uaddr, op, val)`，三个参数走的寄存器是 rdi、rsi、rdx。`kernel/syscall/sys_futex.cpp` 里实现的处理只有两个操作号，两个操作号的名字是 kFutexWait 与 kFutexWake，它们的值分别是 0 和 1，操作号里混进的 PRIVATE 标志位按 128 掩掉，私有和共享在这个实现里走的是同一条路。对齐的检查打头，uaddr 不是四字节对齐的答 -kEinval。WAIT 分支查的是用户地址可读，不可读的话答 -kEfault，然后的事是进 WaitOnUserWord。那个词的当前值不等于期望值的话，调用者拿回的是 kWouldBlock，到了系统调用的边界就翻成 -kEagain，用户态的快速路径就在这里落空回退。WAKE 分支也要过用户地址可读的检查，不可读的话同样答 -kEfault，然后调的是 WakeUserWord，叫醒多少个人由 val 说了算，函数把实际叫醒的个数返回去，系统调用交回去的就是这个数，别的操作号的答案一律是 -kEnosys。

内核里的等待结构在 `kernel/proc/futex.cpp`，整张表的容量是 256 个桶，桶号的算式是 `(uaddr >> 2) & 255`，按字的粒度摊开。整张表只有一把 Spinlock 的门，futex.cpp 的注释给出了理由：一个核的手里从来不会同时握着两个桶，这样 lockdep 那边的锁序故事永远只有一把锁，锁序的描述就简单了。桶里挂的是 futex_wait_link，跟锁族的 sync_wait_link 是两个节点，各自的链互不干扰。停车走的是 ParkOnWord，还是咱们熟悉的配方，开场的那道工序还是 IrqGuard，门锁里核对的是 present 与 expected，不等的话答 kWouldBlock，等就把 uaddr 记进任务的 futex_uaddr、入桶、prepare_to_wait，放锁之后的切换照旧是 schedule_blocked。唤醒走的是 WakeUserWord，扫桶的动作发生在门锁的里面，只叫醒 futex_uaddr 与目标地址严格相等的任务，同桶里别的字上的等待者不受牵连，因为入桶时记的就是精确的地址。锁族的成套纪律在这里一样有效，持着门锁睡觉是 Check 级的错误，等待链的移交语义也照旧。

还有一条退出时的清道 `ClearTidWord`，它接的是线程死掉时留下的对接。任务死掉的时候，它 clear_child_tid 记着的用户字被写零、再按那个地址叫醒一个等待的人，等线程结束的对接方等的就是这一下。写下去之前要过一串的核验，字要在用户地址的上限以内，是规范的地址，任务还得攥着自己的地址空间，落在可写并完整盖住这个字的虚拟内存区间（VMA）里，核不过的话就作罢，核过了才 `CopyToUser` 写零，剩下的叫醒交给 WakeUserWord。tid 字一侧的退出配合，咱们在[进程与调度器](process_and_scheduler.md)的退出一节里见过一对，exit_current 清的就是这个字，唤醒也是顺手做的，两边的接口也就对上了。

## 中断保护：IrqGuard 与它的纪律

锁族跟中断打交道的地方有好几处，好在指令只有咱们需要认的一对，名字写在 `irq_guard.hpp` 的里面，一头写的是 `SaveAndDisableIrq`，另一头写的是 `RestoreIrq`，咱们认准它们就够了。`kernel/arch/x86_64/irq_guard.hpp` 的注释交代了它们的实现方式：一份声明配两份的定义，挑哪一份是链接期的事，内核装的是真的 pushfq 加 cli，host 测试世界装的是什么都不做的空壳，锁和调度器的完整状态机就这样在普通的电脑上跑起来了。

IrqGuard 是这对指令的 ScopedGuard 壳，进场的时候截快照关中断，出场的时候恢复快照。纪律的要点在出场的半下，咱们把要点摊开看：恢复是按快照来的，就算进场时中断本来就是关的，出场的时候也得保持关着，条件恢复让提前开中断的经典错误在这个形状里写不出来。锁族拿它当停车场的外墙，Mutex、Semaphore、Completion 的停车都由它罩场，Spinlock 自己把快照存在 irq_state_ 里、unlock 的时候恢复，道理是相同的。

EnableIrqAndHalt 是成对的动作“开中断再停住”，它跟锁的关系，irq_guard.hpp 的注释给了两条不能碰的线。头一条的红线是拿着自旋锁调它，注释只给了禁令没给理由，咱们能接上的是登记表的那半：持锁停着就是拿着锁睡觉，SpinLedger 的停车检查拦的正是这一类死锁。另一条的红线是分成 RestoreIrq 加单独一条 hlt，x86 的 STI 带一拍指令阴影，sti 之后的头一条指令执行完才认中断，hlt 正好卡在阴影的这一拍里，所以睡眠和开中断是原子的一对，分了家就重新打开了这个原语专门要关掉的丢唤醒窗口。注释还提醒咱们，无关的中断也可能把停着的任务叫起来，等条件的人醒来要重查一遍条件再决定是不是接着停，Completion 的 wait 醒后就重读一次状态，照的正是这个形状。

## 源码位置

- `kernel/proc/sync.hpp`、`kernel/proc/sync.cpp` — Spinlock、SpinLedger、DefaultSpinLedger、Mutex、Semaphore、Completion
- `kernel/proc/lockdep.hpp`、`kernel/proc/lockdep.cpp` — LockOrder 锁序图、held 链、dump 打印桥
- `kernel/proc/futex.hpp`、`kernel/proc/futex.cpp` — futex 桶表、ParkOnWord、WaitOnUserWord、WakeUserWord、ClearTidWord
- `kernel/syscall/sys_futex.hpp`、`kernel/syscall/sys_futex.cpp` — 202 号系统调用、WAIT 与 WAKE 两个操作号
- `kernel/proc/scheduler.hpp`、`kernel/proc/scheduler.cpp` — prepare_to_wait、schedule_blocked、block_current、unblock 与停车许可检查
- `kernel/arch/x86_64/irq_guard.hpp` — SaveAndDisableIrq、RestoreIrq、IrqGuard、EnableIrqAndHalt
