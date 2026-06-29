# 2026-06-27 F7 sys_ping #DF 根治 — ping 改可注入泵(yield)+ 常驻 net_poll kthread 【回补】

## 背景

F7 网络栈 M1/M2/M3 在 worktree `worktree-f7-net-ping` 收官时,真 `ping 10.0.2.2`
已在测试内核里干通(run-kernel-test 947/0)。但合到主线 `feat/f10-tty-dyn` 后,
**生产路径一跑 shell `ping` 就 #DF 卡死**——而 run-kernel-test 全绿。这又是一例
「测试绿、生产崩」,且这次盲区很隐蔽。

## 根因(为什么 syscall 里 sti/hlt 会 #DF)

旧 `cinux::net::ping()`(net_init.cpp)在 `SYS_ping` 里自己跑 `sti / hlt + poll`
自旋。链路是这样炸的:

1. SYSCALL 入口 `SFMASK=0x200` 清掉 IF,`sys_ping` 进来时 **IF=0**。
2. `ping()` 重新 `sti` → 中断打开 → LAPIC timer IRQ(0x30)被投递。
3. `lapic_timer_handler` → `Scheduler::tick()` → `schedule()` **中途把当前任务切走**。
4. syscall 的陷阱帧(user RIP/RSP/RFLAGS)此刻正**压在 `%gs:0` 的 per-CPU 内核栈上**;
   被切进来的任务复用这条栈,把陷阱帧写花。
5. 等 ping 那条任务被切回来、`sysretq` 弹帧 → 弹出垃圾 RIP/RSP → **#DF**(shell ping 卡死)。

一句话:**syscall 里 sti/hlt 自旋,等于自己制造一个能在 syscall 帧还压在栈上时抢占自己的中断。**

### 为什么 run-kernel-test 漏(关键盲区)

测试内核的 LAPIC-timer 处理器**故意不调 `Scheduler::tick()`**(只 EOI,test_net.cpp
里有原话注释)。所以同样的 sti/hlt+poll 循环,**在测试里永不触发调度**,自然过;但
生产内核 timer 会 tick,一 tick 就把陷阱帧砸花。这正是 F-VERIFY「测试 ≠ 生产」的铁证
——也是当年让 e1000 RX「绿」(批b 读 RDH 收得到包是 filter-dump 副作用)的同一个 hack
的延伸:测试 timer 不 tick 掩盖了 e1000 RX 的真问题,也顺带掩盖了这个 #DF。

## 目标

让 `sys_ping` 在生产里安全(不 #DF),同时**保住**真 SLIRP 来回的测试覆盖(不能为了
安全把 ping 测试改成 mock 假装收到 reply),并诚实反映「内核目前没有 ms 级定时器唤醒」
这个事实。

## 实现(`d33a248`,四个文件)

核心思路:把 `sti/hlt` 从 syscall 里**挪出去**——syscall 改走 `yield`(经 `Task::ctx`
切换,跟 TTY 阻塞读 `console_tty_read` 同一条安全路径);`sti/hlt + poll` 收到一个**常驻
内核线程** `net_poll` 独占。内核线程被抢占走的是 `Task::ctx`,不是 syscall 陷阱帧,所以
`sti/hlt` 对它是安全的。

### 1. ping() 收可注入泵(`net_init.cpp` + `net_init.hpp`)

`ping()` 多收一个 `RxPump` 参数(`using RxPump = bool (*)();`——驱动一步 RX、reply
到了就返回 true 的回调)。

- **生产默认 `pump_yield`**:`Scheduler::yield()` + 查 `g_icmp.reply_count()`。**不
  sti/hlt**。reply 由常驻 `net_poll` kthread 排水推过来。
- **超时 = 有界重试预算**(`kRounds=200` 重发 × `kPumpsPerRound=4` 排水),耗尽返回
  `-ETIMEDOUT`。这里**诚实不造假**:内核现在没有 timer-wheel / ms deadline(futex、
  waitpid 都是无脑无限等,连 futex 的 timeout 参数都被丢),所以不假装能给一个真实 ms
  截止时间,只给「重试预算」这种诚实的有界等待。真 timer-wheel 是后续里程碑的事。
- `g_default_pump` 默认指 `pump_yield`;`set_default_rx_pump()` 供测试覆盖默认值。

### 2. 常驻 net_poll kthread(`net_init.cpp`)

新增 `net_poll_entry()`:死循环 `g_stack.poll() → sti → hlt → irq_disable`,不断排水 +
让 QEMU main loop 跑起来好让 SLIRP 投 ARP/ICMP reply。`start_poll_driver()` 在
`Scheduler::init()` 之后起一个 `net_poll` 任务入队(无 NIC 则 no-op)。

### 3. main.cpp 起 kthread(`main.cpp`)

`kernel_main` 在 `Scheduler::init()` 之后、`kernel_init_thread` 之前调
`cinux::net::start_poll_driver()`。kthread 入队后,`run_first()` 开始派发时它就跑起来,
在后台给 ping 的 yield 泵排水。

### 4. 测试缝:rx_pump_sti_hlt + set_default_rx_pump(`net_init.hpp` + `test_net.cpp`)

测试内核**不跑 net_poll kthread**,所以 ping 的 yield 默认泵在测试里永远等不到 reply。
于是把旧的「内联 sti/hlt+poll」排水逻辑**单独拎出来**成 `rx_pump_sti_hlt()`:

- 测试里 sti 安全(timer handler 不 tick,前面盲区那段)→ `test_net.cpp` 里
  `set_default_rx_pump(rx_pump_sti_hlt)` + ping 显式传 `rx_pump_sti_hlt`。
- 这样 `test_production_ping` 仍是**真 SLIRP 来回**(不是 mock),sys_ping(传 nullptr
  → 走被覆盖的默认泵)也被覆盖到。
- 注释里明确写清:**测试安全、生产里 `rx_pump_sti_hlt` 是 #DF 危险品,只能由 yield 泵
  替代**——给后人留路标。

## 验证

- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg
  绿(单核 + `-smp2` 各 ALL PASSED),`test_production_ping` 仍真 ping 通 SLIRP。
- 用户 `make run`(生产路径)验:真 `ping 10.0.2.2` 收到 reply;`ping 10.0.0.2`/不可达
  地址**干净超时回 shell**(不再楔死)。#DF 根治。

## 关键教训(可复用,本批最大价值)

**任何「syscall 里 sti/hlt 自旋」都会引出同款 #DF**——只要中断能在 syscall 陷阱帧还压在
per-CPU 栈上时打断你,`sysretq` 就会弹花。正解是固定套路:

1. **syscall 里别 sti/hlt,走 `yield`/阻塞**(`schedule_blocked` 那条经 `Task::ctx` 的
   路径,跟 TTY 阻塞读一样)。需要等待的事件,由别人(常驻 kthread)在背后推进。
2. **`sti/hlt` 移到常驻内核线程**——它被抢占走的是 `Task::ctx`,不是 syscall 帧,放心 sti。
3. **driven 方法(可注入泵)是让「#DF 安全的生产主线」与「非抢占式测试」共存的标准缝**:
   生产注 `pump_yield`,测试注 `rx_pump_sti_hlt`/mock。同一个 `ping()` 在两条世界线里
   各自安全且真实,不用为安全牺牲测试的真来回覆盖。

附带教训还是那条**测试 ≠ 生产**:测试 timer 不 tick 是真「绿」的常见伪装,任何依赖
「timer 是否 tick」的行为差异(调度、超时、IRQ 驱动的状态机),都得在生产路径单独验证,
别让 run-kernel-test 的绿盖住它。

## 残留 / 后续

- **真 ms 级定时器唤醒**:本批用「重试预算」诚实兜了超时,但内核没有 timer-wheel,
  `ping` 的「x ms 超时」语义要等后续定时器里程碑才能给真 deadline。
- **中断替 polling**:net 仍是 polling(F5-M6 批c 那条线,常驻 kthread 轮询),真 IRQ
  驱动 RX 是后续。
- **UDP/TCP(F7-M4/M5)、Socket(F7-M6)**:F7 后续里程碑。
