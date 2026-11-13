# armv7 端口骨架

状态：能启动、能跑用户态、能按时间片抢占（2026-11-12）。这份记这一版活着
的东西、两条陷阱帧路径的分工、以及踩过的几处，方便后面接着做。

## 总览

| 项 | 现在的样子 |
| --- | --- |
| 系统调用 | 17 个号：exit 1、fork 2、read 3、write 4、open 5、close 6、lseek 19、getpid 20、brk 45、getppid 64、signal 67、getticks 78、mmap 90、munmap 91、wait 114、sched_yield 158、sleep 162 |
| 任务槽 | 8 条：开机 4 条，后 4 条留给 fork，退出后回收复用 |
| 用户地址 | 代码 0x40001000（预留三页）、栈 0x40004000、按需窗口 0x40009000、brk 0x4000C000 到 0x40010000、mmap 0x40010000 到 0x40100000 |
| 用户代码段 | 4180 字节、两页；链接期断言不超过三页 |
| 断言 | arm_boot 107 条期望，全按字面量比对；同名 `-D` 在配置期直接报错 |
| 其它验证 | x86 宿主 86 个测试、内核镜像大小核对 |
| 复跑 | `scripts/check-armv7.sh --repeat N`，失败自动留档 |

## 这一版有什么

`kernel/arch/armv7/` 下的自有镜像，走 `cmake/arch/armv7.cmake` 的车道。
一次 configure 带一套交叉工具链，root 在车道之后返回，不碰 x86 的构建。

- 启动：`start.S` 关中断、给 IRQ、FIQ、ABT 各配一个栈、开 VFP、清 bss；
  `link.ld` 把镜像放在 0x80000000，栈与 `.user_text` 也在这里排。
- 串口：`uart.cpp`，i.MX6ULL UART1，115200 8N1。UART1 的根时钟按 PLL3 的
  80 MHz 算，UFCR 二分频到 40 MHz，UBIR=15、UBMR=346 得 115273 baud。
- 定时器：`timer.cpp`，通用定时器。CNTFRQ 读频率、CNTPCT 读计数、
  CNTP_TVAL 定时。CP15 的选择子是关键：TVAL 是 opc2=0，CTL 是 1，CVAL 是 2。
- 中断：`gic.cpp` 管 GIC-400 的 distributor 与 CPU interface，定时器挂在
  PPI 29/30；`vectors.S` 是八槽向量表，每槽一条分支到表外的桩。
- 地址空间：`vmm_bringup.cpp` 在运行时建表。固件清单里的 RAM 铺成 1 MiB
  段，低 512 MiB 铺成设备段，内核与设备段只给 PL1；用户页走各自的二级表。
  另有 `page_entry.hpp` 与 `page_walk_shape.hpp` 提供短描述符的形状与谓词，
  通用走法 `kernel/mm/page_walk.hpp` 在这一层上工作。
- 物理内存：`kernel/mm/pmm.cpp`，BootInfo 由 DTB 的内存区间造出来，
  256 MiB 报 65536 个可用页。
- 堆：`kernel/mm/heap.cpp` 与 `heap_runtime.cpp`。块头按指针宽度写，32 位上
  是四个字；运行时从 PMM 拿页、映射、grow。静态 arena 只给自检用。
- 控制台与 DTB：`console.cpp` 把控制台的输出接到串口；`fdt.cpp` 读内存区间。
- 异常：`faults.cpp` 读 DFAR 与 DFSR 报故障地址与原因；用户态来的故障交回
  调度，内核态来的停机。`isr.hpp` 把 DFSR 与 SPSR 折成 permission、write、
  user 三个语义位，其余部分只认这三个位。
- 上下文：`context.hpp` 与 `context_switch.S` 搬 r4 到 r11、sp、lr、SPSR；
  `ContextSwitchIntoUser` 走异常返回，由 SPSR 决定落在哪个模式。
- 每核数据：`per_cpu.hpp` 用 TPIDRPRW 找块，`kernel_stack.hpp` 的栈顶记在
  块里；armv7 没有 TSS，这是与 x86 差别最大的一处。
- 用户态：`user_mode.cpp` 建每任务的空间与用户页，空间由
  `mm::AddressSpace` 的 init 与 map_user 生成，`user_trap.hpp` 记陷阱帧，
  `user_entry.S` 做进门与恢复。系统调用一律走陷入路径，`svc.cpp` 的两张表
  决定谁处理：read（3）、write（4）就地作答、同一张帧返回；getpid（20）、
  brk（45）、mmap（90）、munmap（91）、close（6）、exit（1）、wait（114）、
  sleep（162）、sched_yield（158）交给调度器。没登记的号报一行并回 -14。
  负值收在 `errno.hpp`：指针不对 -14（EFAULT）、描述符没开 -9（EBADF）、
  窗口没地方 -12（ENOMEM）、参数形状不对 -22（EINVAL）；等待的"还没好"
  与"超时"不是错误，留在调度器里（-1 与 -2）。每任务一张描述符位图（开局
  0 到 2 开着）、一个只增的 brk 窗口（0x4000C000 到 0x40010000）与一个
  可给地址提示的 mmap 窗口（0x40010000 到 0x40100000，munmap 可还）。
- 用户指针：`user_access.hpp` 先验整段在用户顶以下、不溢出，再逐页看活动表
  里是否对用户可读；`write` 与 `read` 都走它，坏指针返回 -1 而不是把内核
  地址当用户缓冲。
- 地址空间的生与死：`KernelTables::take_root` 与 `release_root` 由端口给
  尺寸与对齐，`AddressSpace::teardown` 按 `IsUserSlot` 找用户槽并把帧、
  表页、根表还回去；自检验的是拆完空闲页数回到原点。
- 调度：三条用户任务，第一条的空间自己建，后两条由它克隆；栈页在克隆之后
  私有化，参数写各自的下标。任务收在 `UserTask` 数组里，装空间、陷阱帧、
  状态与退出码；`next_runnable` 是唯一的选择逻辑，让出、抢占、故障共用。
  时间片一个 tick，从用户态来的中断里递减，到零就让出。wait 的目标没结束时
  任务转入 kBlocked，调度器跳过它；任务结束只唤醒等它的那些（挂起时记下
  位图）。wait 的第一个参数是位图，位上是等哪几条，返回先结束那条的下标；
  第二个参数收状态。等待带截止：第三个参数是拍数，0 表示默认二十拍；到期由
  tick 唤醒并把返回帧里的 r0 写成 -2，正常唤醒是 -1，用户侧据此分清"还没好"
  与"超时了"。sleep（162）复用同一套截止：不设等待目标，到期返回 0。没有
  可运行任务但还有人挂起时，按超时把它们叫醒，免得死锁。任务结束（正常退出或故障）
  之后，调度器切走它再把它的空间 teardown 并释放；建任务之前与全部拆完之后，
  空闲页数必须相等。另有 `task_switch.cpp` 的两条内核侧任务轮转，
  留作上下文切换的独立自检。
- 平台缝：`kernel/platform/<名字>.hpp` 只 include `cinux/platform/<名字>.hpp`，
  这个文件由根构建按 `CINUX_ARCHITECTURE` 生成，指向该端口的聚合头。七条
  中立头（bus、clock、firmware、interrupt、io、ps2）都走这条缝。
- 缺页的修复门：用户态来的数据异常存整张帧，`faults.cpp` 读 DFAR 与 DFSR，
  把故障交给一个修页钩子；钩子修好就把同一张帧装回去，出错的那条指令重跑。
  钩子只认两段窗口：按需补页的 0x40009000，以及共享页的写时复制。
- 写时复制：共享帧映射成用户只读（AP 0b10），写故障时复制一页、当前空间
  改指副本并给可写、旧帧账减一；`cow_release.cpp` 的小表只记被共享的帧。
  三条任务共享 0x40008000，各自写、跑过若干次抢占再读回，都报
  `task N keeps its page`。
- 克隆：`clone_space.hpp` 遍历父空间的用户槽，子空间为每张二级表新开一张，
  叶子帧逐页 Retain，父子都降到用户只读。自检里父空间挂一页并克隆，两边
  翻译到同一帧、账本是两位主人；拆子空间之后父空间仍可翻译；再拆父空间，
  空闲页数回到原点。只支持小页叶子，用户窗口里出现段就拒绝克隆。

## 两条陷阱帧路径

armv7 的栈是按模式 bank 的，用户态与 SVC 各有一套，所以"从哪里进来"决定
存什么：

- 用户态进来（svc 的陷入路径、IRQ 且 SPSR 是用户模式）：存整张帧，
  r0 到 r12、用户 sp、用户 pc、SPSR 共十六个字。用户 sp 从 System 模式读，
  因为 System 与用户共用 bank。存完整张才敢换人，抢占也才敢落在任意指令
  之间。
- 内核态进来（IRQ 且 SPSR 不是用户模式）：只 push r0 到 r3 与 lr，处理完
  原路返回，不换人。内核栈没有第二套，换人没有意义。

这个分工是踩出来的，见下面第十一处。

## 自检清单

镜像里按顺序跑这些，任意一条失败都会在串口上说得出来：

| 自检 | 报的那行 |
| --- | --- |
| 页大小与定时器 | `page 4096 bytes, timer: freq 62500000 Hz` |
| DTB 内存区间 | `dtb memory 0x80000000 + 0x10000000` |
| MMU 与两级翻译 | `mmu on`、`two-level page ok` |
| PMM | `pmm free pages`、`selfcheck pmm ok` |
| 控制台 | `console up, 65536 pages managed` |
| 静态堆 | `selfcheck heap ok` |
| 按页生长的堆 | `paged heap live, probe 114514` |
| 走法映射一页 | `mapped page ok` |
| 上下文往返 | `context switch ok` |
| 每核数据 | `per-cpu ok` |
| 地址空间的拆分 | `space teardown ok` |
| 共享帧的账本 | `cow ledger ok` |
| 一帧挂进两个空间 | `shared frame ok` |
| 用户子树的克隆 | `fork clone ok` |
| 两条内核任务轮转 | `two tasks ok` |
| 两条用户任务与抢占 | `preempt to task`、`task 0 exited with status 7`、`task 1 faulted` |
| 坏指针 | `svc write refused 80000000 + 8` |
| 读 | `svc read 0 bytes` |
| 按需补页 | `fault repaired at 40009000` |
| 写时复制 | `cow copy at 40008000`、`task 0/1/2 keeps its page` |
| 任务的建与拆 | `tasks pages 65186 -> 65186` |
| 等待与挂起 | `task 0 blocks on 1`、`wait 2 -> DEAD` |
| 自身标识 | `getpid 3`、用户侧 `pid 3 ok` |
| 扩堆 | `brk -> 4000E000`、用户侧 `brk ok` |
| 匿名映射 | `mmap 40010000 len 2000`、用户侧 `mmap ok` |
| 地址提示与回收 | `mmap hint 40020000 is taken`、`munmap 40020000 pages 1`、用户侧 `mmap hint ok` |
| 只读映射 | `ro copy at 40030000`、用户侧 `ro copy ok` |
| 参数检查 | `mmap flags 2 refused`、用户侧 `flags refused` |
| 描述符 | `close 2`、`svc write refused -9 at`、用户侧 `ebadf ok`、越界回 EBADF（`fd range ok`） |
| 文件 | 只读 `/hello` 与可写 `/scratch`；`open`、`lseek`、只读拒写、开满回 ENOMEM 都有自检 |
| 等任意一条 | `task 3 blocks on 4`、返回先结束那条的下标 |
| 等待超时 | `task 0 wait timed out`、用户侧 `self wait timed out` |
| 睡眠 | `task 0 sleeps 3 ticks`、`task 0 woke from sleep`、用户侧 `slept` |
| 用户态往返 | `user mode round trip ok` |
| 内核态缺页 | `kernel fault probe`、`data abort at 51000000 status 805 code 2` |

## 验收

`cmake --build build-armv7 --target run` 起 QEMU 的 mcimx6ul-evk；
`ctest --test-dir build-armv7` 跑 `arm_boot`，逐条对上面那些行。

x86 那条线不受影响：`cmake --build build --target test_host` 是 86 个用例，
`kernel_bin` 的镜像大小另有校验。

## 踩过的几处

按遇到的时间排，每条只记"现象与真正的原因"：

1. 安全态。核起来在 secure 状态时，通用定时器的比较中断落在 PPI 29，
   默认在 group 0，走 FIQ；只开 IRQ 收不到。IRQ 与 FIQ 都接上才行。
2. 异常模式的栈。IRQ、FIQ、ABT 各有独立的 sp，不配的话处理函数第一句 push
   就写进垃圾地址，表现是"中断进了但什么也不打"。
3. CP15 的选择子。c14,c2 的 opc2 写错一位，读到的是别的寄存器，看起来像
   定时器不工作。
4. FDT 的属性结构。顺序是 tag、len、nameoff，len 在 nameoff 之前；写反了
   会读出空的内存区间。
5. `-lgcc` 的位置。放在链接选项里会排在目标文件之前，`__aeabi_uldivmod`
   就找不到；用 `target_link_libraries` 才对。
6. 堆的块头宽度。按字节写死在 32 位上会静态断言失败；改成按
   `sizeof(unsigned long)` 算，两个端口共用一套块语法。
7. L1 表对齐。一级表是 16 KiB 且要 16 KiB 对齐，PMM 给的单页不保证；切到
   未对齐的根表会让取指直接停住。现在拿八个连续页、取里面的对齐窗口。
8. 挑对齐页时立刻把不要的页还回去，PMM 会把刚还的那页原样再发一次，循环
   原地打转。要先记账、找到再还。
9. 陷阱帧恢复时 `ldmia {r0-r2, r4-r12}` 跳过 r3，后面的寄存器整体错位一个
   字，r4 拿到的是 r3 的值。表现是参数跑几轮之后变成 0，任务看起来正常退出。
10. 用户侧 svc 的内联汇编只声明 r0 是输出，编译器把轮次计数养在 r1、r2、
    ip 里；切回来之后那些是内核的残值。把 r0 到 r3、ip、lr 都声明成会被改
    才对。
11. 内核态中断走陷阱帧，存下的 sp 是 System 模式（用户 bank）的栈，内核
    真正在用的 SVC 栈没被保存；恢复之后内核上下文被换到一处不是它的栈上，
    循环看着在跑，其实早就不在原处。修法是按 SPSR 分路，见上面那一节。
12. 用户入口不能内联。第一版把切模式与跳转写成一个内联函数，恰好排在一次
    打印之后，`stmia` 存下的 lr 是那次打印调用的返回地址，用户一退出就跳
    回函数中间，来回死循环。独立成 `user_entry.S` 里的函数才对。
13. 抄内核槽位的循环用了叶子表的槽数。x86 的顶层也是 512，看不出问题；
    armv7 的一级表有 4096 槽、内核在 2048 号槽上，只抄 256 个就把内核丢了，
    切过去直接停住。要用 `EntriesAt(kUpperLevels[0])`。
14. 回收的递归按 x86 的层数写死。两级表上"层索引超出一级表集合"才是叶子
    表，套 x86 的式子会把表页当帧释放。
15. 判据写反过两次。第一次要求"拆完空闲页数变多"，其实持平才对；第二次是
    根表那边还漏着四页，不管怎么判都不会平。先把漏的页还回去，判据才站
    得住。
16. 判断异常来源用了 r3，而 r3 是用户寄存器。用户故障时它先被改成模式值，
    重跑的指令就跑到那个数上去了（串口里是 fault caught at 10）。判断前先
    把 r3 压栈，用户路径再取回。
17. 修页钩子起初没有窗口，把自己安排的跨表故障也修了，那条验收当场失效。
    钩子只该认它负责的那几页。
18. 试过把 IRQ、svc、数据异常合成一个入口、一个宏保存整张帧再按种类分发。
    那一版在这一轮里挂住过，标记也看不出进了哪一步，最后整段退回，只给数据
    异常单独留了一条保存路径。这个方向未必错，但没能在这轮里验证，所以不
    留在树上。
19. 内核态 IRQ 先把 r0 改成槽位号，再交给分发去 push，压进栈里的就是那个
    槽位号，回到内核时 r0 已经变了。用户态那条也有同样的味道：模式判断用
    的 r3、传参的 r1 与 r2 都在保存之前被改。现在内核态那条自己保存
    r0 到 r3，用户态与 svc 各自有一份保存块，种类在保存之后才写。
20. 内联汇编改完没确认链接成功就开始解读串口。`cps#0x13` 少一个空格，
    编译一直失败，跑的一直是旧镜像；于是把"新加的代码没执行"当成内核继续
    执行的落点有问题，追了一整轮。链接失败时 ninja 会停下，别跳过那行输出。
21. 用户代码段长过一页之后，拷贝越界写坏了相邻物理页；症状是"加一段用户
    代码就内核停机、删掉就好"。现在按页分配与映射。
22. 测试参数里带空格的 `-D` 值没加引号，CMake 按空格把它切成多个参数，真正
    传进去的只有第一个词。于是很多断言长期只查了开头一个词。修法是整体加
    引号；顺便逐条核对：三十四条期望在输出里都能按整句找到，也都有对应的
    检查分支，其中一条早就不存在的 `svc write matched` 换成现在的
    `svc write`。
23. 用户代码里 `char name[8] = {...}` 这种只初始化、之后再没改过的局部数组，
    会被编译器优化成只读数据放进内核的 .rodata；用户态照这个地址读就缺页。
    现在这类字符串一律逐元素赋值，让它落在用户栈上。
24. 断言用 `MATCHES` 比对，期望串因此被当成正则。`mmap` 那行写成
    `mmap 40010000 + 2000`，加号成了量词，本地看着对、测试却红。现在换成
    `string(FIND)` 的字面量查找，串里有什么就比什么，失败时把期望串与整段
    输出一起打出来。

## 调度器的上提之路

`kernel/proc/scheduler.hpp` 里已经有一个通用调度器：任务类、调度类、退出码、
等待子任务、时间片，还有一处可注入的 `SwitchSink`，用来把"真正切换"这部分
留给端口。也就是说它本来就按"共用策略 + 端口执行器"设计的。

这一版 armv7 用的还是 `user_mode.cpp` 里的迷你调度器：三个状态、位图等待、
截止拍数、按目标唤醒。它能跑，但和通用调度器是两套。

要用上通用那套，缺的是一处 armv7 的 `SwitchSink`：保存当前任务的世界（陷阱
帧）、换 TTBR0、再装下一个任务的世界。真去看代码之后，阻塞点比原先估的多：

- `SwitchSink` 要四个方法：`switch_tasks`、`enter_from_main`、`return_to_main`、
  `reclaim`，x86 由 `InstallKernelSwitchSink()` 在 `kernel.cpp` 里注入。
- `kernel/proc/task_runtime.cpp` 是 x86 味道的：它直接读写 `ctx.fs_base` 与
  `ctx.gs_base`、用 `StartPlan` 的 rip 语义、调 `StoreTaskAddressRegs` 与
  `LoadTaskAddressRegs`，还直接摸 `PerCpu::kernel_rsp`。armv7 的上下文是
  r4 到 r11 加 sp、lr、SPSR，没有段基址，每核块里也没有 `kernel_rsp`。
- `PrepareTaskContext` 也是同一份文件里的 x86 实现，写的是 rip 与 FXSAVE64
  的初始值。
- `Task` 里的上下文类型确实是 arch 给的 `CpuContext`，这一点是好的；坏的
  是建上下文与切换这两段都被 x86 的具体字段绑住了。

所以接入的路径不是"写个 sink 就完"，而是先把 `task_runtime` 里那两段
（建上下文、切换）抽成端口实现，再写 sink，最后才谈把用户任务搬过去。

已经做完的：建上下文（`kernel/arch/task_context.hpp`）、切任务前的栈顶与
地址寄存器（`task_switch.hpp`）、三参数切换的端口化，以及 armv7 上下文末尾
补的两个地址寄存器字段。做完之后再拿 `task.cpp`、`task_runtime.cpp`、
`scheduler.cpp` 试编，还差这几样端口侧的东西，也都补上了：
`smp_control`（单核空实现）、`FpuImage` 占位、`PerCpu` 补齐共享代码用到的
字段、`TaskTrampoline`（r4 放入口、r5 放退出钩子）、以及终端的车道里也要
生成 `checks_config.hpp`。

补完之后编译是过的，链接过不去：`Task` 这个控制块带着一整套依赖，
`fs::FileTable` 的构造与析构、`proc::SendSignal`、`futex::ClearTidWord`
都要一起进来。这三样各自又牵出文件系统、信号与 futex。也就是说把通用调度器
接上，等于把这几块一起搬进臂上镜像。这一版没有搬，先把依赖记在这里。

在没做这一步之前，迷你调度器就是这一版的真调度器，笔记与自检都按它写。

## 还没能验的一条

`read` 分两条路：没数据时挂起、到期回 0（这条有自检），以及数据到了就在
tick 里读进用户缓冲（这条没有）。查过 QEMU 的模型源码（[hw/char/imx_serial.c](https://git.progress-linux.org/horok/qemu/raw/commit/a3199af6f93aefcd911216da31aa841a737f1190/hw/char/imx_serial.c)），
三条与这里有关的语义：读 `URXD` 会清掉 `USR2` 的 RDR；`UTS1` 的 RXEMPTY
才是"队列空不空"的权威位；写 `UCR2` 时不带 SRST 会触发一次整片复位。
`UartReceiveReady` 因此从看 `USR2` 的 RDR 改成看 `UTS1` 的 RXEMPTY。

即便如此，把一行稳定地送进接收 FIFO 仍然没做成。三条路都试过：

- stdio 的 `INPUT_FILE`：RDR 一直是 0。
- unix socket 串口：同样看不到数据。
- pty 串口：这一条有进展。开机阶段的轮询探针确实读到了 `h`、`i`、`\n`；
  在开机后约 0.15 到 0.35 秒之间写一行，也能让第一个读者拿到 `read 3 bytes`。
  但只要改成"看到横幅再写"或"等读者挂起再写"，数据就进不了 FIFO；在测试
  框架里同一个写法时好时坏。这种时序依赖不适合当验收，所以 pty 驱动没有留在
  树上，`read` 的数据到达分支仍只有代码、没有覆盖。

这轮留下的是一道防线：跑之前先删旧日志，跑完若日志不存在或为空直接失败。
之前 runner 起不来时测试 0.01 秒就"通过"，断言读的是上一轮留下的日志。

## 祖孙三代：用户代码段长过一页

想让 4 号子任务再 fork 一层，长出孙任务。加进去之后每次都在
`data abort at FFFFFFEC status 805 code 2` 上停住，内核态停机，连调试打印
都没出来。撤回之后换最小重合：只 fork 不再等，不崩；把等待加回来就崩。

病根不在 fork，也不在等待：用户代码段已经长到 `0x1078` 字节，超过一页，
而 `setup_task` 只分配一帧、只映射 `code_va` 一页，拷贝那一圈的循环却按整个
段长写，于是把紧挨着的物理页写坏了。加代码就崩、删代码就好，正是这个道理。

改法：按 `kTextBytes` 算页数，逐页分配、逐页拷贝（最后一页补零），紧跟
`code_va` 往后映射；栈因此从 `0x40002000` 挪到 `0x40004000`，把中间三页
留给代码。另外加了两道界：链接脚本里 `ASSERT` 段长不超过留给代码的三页，超了直接
链接失败；`setup_task` 里也按页数核对一次，不够就打印并拒绝建任务。开机时
还会报一行人读代码段长度与页数（现在是 4180 字节、两页）。

改完祖孙三代一次通过：`grandchild ok`、`task 6 exited with status 4`
与 `two levels ok`，页数仍回原点。

## 可写文件：第二次做成了

上一轮加可写文件时，那次 open 既没有成功行也没有拒绝行。复盘下来是自检块
放得太晚：前面那轮"把 3 到 7 号槽占满"的检查把表填住，open 走了"没有空槽"
那条分支，而那条分支当时不打印任何东西，于是整块静默跳过。

这次先把它放到最早的位置，并且每一步都在用户侧写一行（打开失败、写失败、
比对结果）。一次就通：`open scratch as`、`file write 6`、`writefile ok`。

## 可写文件那条没做成（第一版）

想加一个可写的内存文件（/scratch）：内核里一块 bss 缓冲、描述符记可写位、
write 按偏移写进去、再读回来。加进去之后那次 open 在串口里既没有成功行也
没有拒绝行，只看到进了处理函数，随后用户侧拿到一个小于 3 的返回值，整块
跳过。判断不出是路径内容不对、槽位搜索提前返回，还是别的，一次都没走到
可控的状态。

这段改动整体撤掉，只留一项确定有价值的：open 的路径改成逐字节拷。原先一次
拷 15 字节，用户栈上的字符串若靠近页尾，整段校验会连带失败；逐字节就能在
第一个读不到的地方停下。

可写文件仍待做，重做时应当先把"open 返回什么"在用户侧打出来，再往下接 write。

## 槽位不释放：close 只认 0 到 2 号

给 ramfs 加第三个文件时，自检里成片 `open refused, no free slot`。查下来根因
在 `close`：它还是"只关 0 到 2 号"的老版本，文件描述符（3 号往上）根本没被清
掉，于是每开一次就漏一槽，开到第五个文件就没地方了。

最小序列把这件事钉死：开 hello 拿到 3、关掉、再开——修之前再开拿到 4，修之后
拿回 3。现在 `close` 认任何开着的槽，槽位回收正常，第三个文件的自检也一次通过
（串口里 `reopen ok`、`open readme as`、`readme ok`）。

这个毛病在只有控制台描述符的时候看不出来，因为那三个槽本来就常开。

## x86 描述符表审计：armv7 犯过的毛病那边都没有

方向调整：不再继续给 armv7 的替身加能力，改为先审计 x86 同类逻辑，再造中
立层。审计对象 `kernel/fs/file_table.cpp` 与 `kernel/syscall/sys_close.cpp`。

close 入口 `HandleClose` 直接转发 `FileTable::free`，任意描述符都能释放，
没有"只认 0 到 2 号"的限制。`free` 锁内查槽、detach 挪出、锁外靠持有量析
构释放；`detach_locked` 把 `next_fd_` 回退，所以释放后的编号会被重新发出，
与 armv7 验过的"关掉再开拿回同一编号"同构。

测试盖得全（`test/unit/fs/test_file_table.cpp`，17 个用例）：释放后最低优
先复用、双重 close 被拒、adopt 共享游标（即 dup 的偏移共享语义）、cloexec
位、并发 close 与重用、dup2 校验源、fork 快照、动态增长与降限额。armv7 犯
过的三类毛病（close 只认 0 到 2、槽位泄漏、别名判断错位）在 x86 都不成立，
且各有测试守着。

浅缺口一处：`HandleClose` 本身没有直接用例（`close_range` 有），但它只有
五行转发，`free` 已被充分覆盖。x86 宿主测试 86/86 通过。

## 中立层试编：fs 在 armv7 工具链下 18 缺 1

审计之后按计划做了"先编不链"。方法：从 build-armv7 的 compile_commands 里
取 arm-none-eabi-g++ 的完整 flags，剔除 -c/-o 两个参数，逐个编 kernel/fs 全部
源文件。

结果是 18 个文件里 17 个直接编过：file_table、vfs、ramfs、file_lock、pipe、
ustar、file_world，连同 ext2 全套与 devfs、procfs 的主体。唯一失败的是
procfs_init.cpp，它引 kernel/arch/cpuid.hpp，armv7 的生成头找不到对应文件。
这不是缺一份副本的事：CPUID 的叶与寄存器排布本身就是 x86 语义，procfs 拿它
填 /proc/cpuinfo。中立做法是把"CPU 事实来源"抽成架构钩子，x86 后端用 CPUID
填，armv7 后端用 MIDR 或先给桩，等 procfs 上 armv7 时再做。

再往下做了一层：把编出的目标文件用 ld -r 合并看未定义符号。全模块 91 个符
号，大头是 ext2/devfs/pty 拖来的块设备与输入驱动（Ahci、Nvme、BlockCache、
Framebuffer、Pty、Tty 一串），armv7 没有磁盘，这些先不链。只链第一刀要的最
小组合（file_table、vfs、ramfs、file_lock）时，未定义只剩 13 个，对照 armv7
现有内核的符号表，其中 7 个已经供着：FormatToBuf、AssertionFailed、memcmp、
memset、__aeabi_uldivmod，以及全套 operator new/delete（armv7 已有内核堆）。

真正要补的只有 6 个：memchr、std::terminate、SpinLockPolicy::enter 与
exit、Spinlock::retire 与 set_name。前两个各几行，后四个是 irq_guard 之上
的薄实现。第一刀（把替身的描述符表换成真的 FileTable）的缺口比当初记的
"fs::FileTable、SendSignal、ClearTidWord 三块"乐观得多：那是把 Task 整个
接上时的旧清单，只换文件层的话，6 个符号补完就能链。

## 第一刀前半：fs 真件链入 armv7 镜像

六个符号补完的方式比预想的更省：memchr 与 std::terminate 不用新写，通用层
kernel/lib/mem.cpp 整个文件在 armv7 下直接编过，一套 mem 函数全带上；锁策
略四件也不用抄，kernel/proc/sync.cpp 本身编过，它引出的 Scheduler 四件
（current、prepare_to_wait、schedule_blocked、unblock）由新增的
kernel/arch/armv7/proc_shim.cpp 垫上：current() 返回一个代表主线的静态 Task
桩，三条睡眠路径一律断言拒绝并留名，走到睡眠锁会大声 halt 而不是静默错。

静态 Task 桩带构造，编译器要注册析构，于是补了 \_\_dso_handle 与
\_\_aeabi_atexit 两个桩（内核不退出，注册表收下就丢）。libc_support.cpp 里
旧的 memset/memcpy/memmove/memcmp 与 mem.cpp 多重定义，旧的退役，文件只剩
atexit 桩。

CMakeLists 把 file_table、vfs、file_lock、通用层 ramfs、sync、mem、
proc_shim 挂进 arm_image。链接通过，arm.elf 从约 51 万字节涨到 1081028。验
收：arm_boot 3/3 全绿（断言一条未改，串口行为完全一致，替身照旧服务），x86
宿主 86/86。

armv7 目录自己的替身 ramfs.cpp 与通用层 kernel/fs/ramfs/ramfs.cpp 并存不冲
突：第一刀只链不接线，等切 close 时才动。

## 第二刀：六个系统调用全量切到真件

替身 fds[8] 数组退役，UserTask 改持 `base::Shared<fs::FileTable>`，每个任务
建表后由 seat_console 把 0/1/2 三席直接铸进表里（1/2 带可写位，0 只读），
fork 走 `clone()` 继承。六个调用全部经过真表：close 走 free、fstat 用
ops->stat 现场探测、lseek 先查文件型再用 stat 问大小、open 逐字节拷路径后
经 `vfs().resolve` 与 `lookup_held`，只有 /scratch 用 `make_file_held` 落可
写节点（held 引用在 alloc 成功与否两条路上都释放），read 沿文件路径分 64 字
节搬运。控制台三席的读写行组与替身时代逐字节相同，arm_boot 的 107 条期望一
条没动。替身 ramfs.cpp/ramfs.hpp 删除，镜像里只剩 kernel/fs 这一份实现。

## 三处连环陷阱：错的全不是嫌疑最大的地方

切完之后每个任务创建都崩在 data abort at FFFFFFEC，连环排掉了三个错处才见
到底。

第一处是 PerCpu 自检留下的 0x5A5A 探针值装在 cpu_id 里没复位，之后每一把自
旋锁都往 SpinLedger::depth_[0x5A5A] 越界写。自检结束时补一句 block.cpu_id =
0 即了。

第二处是 seeding 阶段对 make_file 返回的句柄调了 release：那个家族返回的是
借用句柄、不带引用计数，release 一记就把还在树上的节点删了，第三次 make_file
遍历目录时踩进已释放又复用的内存。借 held 语义的只有 make_file_held 与
lookup_held 两条路，open 处理器里那记 release 是对的，保留。

第三处才是 FFFFFFEC 本尊，前后两个真凶叠在一起。一个是通用堆的 footer 在
32 位下是 8 字节，16 对齐被每次分割悄悄推歪，分配地址出现 8 字节错位（探针
里新表落在 0x...B8 上直接看见）；footer 补到对齐粒度后 x86 数值不变（16 依
旧 16），armv7 变 16。另一个是 start.S 只开了 CPACR 的 CP10/CP11 门，没拉
FPEXC.EN：编译器为搬 64 位常量发的 vldr d16 在没点亮的 VFP 上一律按未定义
指令炸，异常处理一路连环坠毁。探针定位到具体指令后补一句 vmsr fpexc 便收
口。地址 0x14 恰是这些越界错位叠出的表象，前面的打印扰动让症状来回变脸，
正是内存错位的典型做派。

## cpuinfo 的架构钩子：解码搬进端口，中立形状只有一份

x86 那边的审计结论：procfs_init.cpp 是通用层文件，却直接调 ReadCpuId 解
vendor/family/model——CPUID 的语义硬编码在 fs 层，armv7 一编就缺
armv7/cpuid.hpp（试编时 18 缺 1 缺的正是它）。修正不是给 armv7 造一个假
CPUID，而是把解码搬回它所属的地方：两个端口各自在 kernel/arch/<port>/
cpuid.hpp 里发布同形状的 CpuIdentity 与 ReadCpuIdentity(ordinal)，x86 用
CPUID 解码（连 flags 名字表一起搬走），armv7 用 MIDR 解 implementer/型号
号/修订号、MPIDR 解亲和号，解不出的字段（brand string、特性表）老实留空。
procfs_init 的 source_nth_cpu 从 60 行解码缩成纯搬运，行为逐字节不变。

armv7 车道加了一条开机探针：读真 MIDR 打 cpu id ARM C07（QEMU 的
Cortex-A7），挂进 arm_boot 期望，解码在新车道上真跑过一次而不只是可编。

## /proc 落进 armv7 的 vfs

通用 ProcFs 实例在 userfs::init 里挂到 /proc，来源只填两个座位：memory 走
mm::Pmm 的通用计数，nth_cpu 用上一节提的共用座位 NthCpuFromIdentity（放
procfs.hpp，x86 的 WireKernelProcSource 也改用它，八行搬运只留一份）。填不
了的座位留空，读出来是安静的空文件而不是编造。init 尾部回读探针经真 vfs
resolve /proc/cpuinfo 读 96 字节并确认带 ARM 字样，打印 proc cpuinfo 96
bytes 挂进 arm_boot 期望。链接上补了 base/src/parse.cpp（procfs 的 lookup
解析十进制 inode 号要用）。

## 通用调度器真件链入，proc_shim 退役

审计结果比预想顺利：kernel/proc/task.cpp 与 scheduler.cpp 在 armv7 下语法直
接全过（CpuContext/FpuImage 的转发头早已就位）。把 task、scheduler、
signals、futex、uaccess、sys_clock、vma 七个通用件编进 arm 镜像，链接只
追出四个缺口，逐个中立化补齐：

- IsCanonicalAddress：armv7/context.hpp 补 32 位规则（高 32 位为零恒真）。
- MoveUserBytes/ArmUserWindows：armv7/user_access.hpp 补对称搬运门——恰好
  一端在用户区才做页表走查，两端同侧拒之；SMAP 座为空操作。
- MonotonicNanoseconds 的 HPET 依赖：提 platform::FineClockNanoseconds()，
  x86 答 HPET，armv7 答 0 退共享 tick。
- boot_wall 的 RTC 依赖：提 platform::BootWallSeconds()，x86 读 RTC，armv7
  无电池钟答 0（纪元即开机）。

proc_shim.cpp 的四个符号与真件撞名，使命结束删除；Mutex 竞争路径补一处
null 任务断言，让 task-less 调用响亮拒绝而不是把空指针穿进等待链。arm 车
道补 -fno-threadsafe-statics（与其余车道同款）。arm_boot 3/3 过，host 单
测全绿。

## 共享调度器第一趟真切换：main 进任务再回 main

task_runtime.cpp（KernelSwitchSink 与 TaskBuilder 的家）在 armv7 下语法直
接全过——它依赖的每个 arch 座早已就位：ArmTaskContext 写 r4/r5/sp/lr，
context_switch.S 的 TaskTrampoline 按同一约定起跳，SwitchTaskContext 单核
同核置 on_cpu=-1，ArmTaskStack/space::SwitchRoot 都在。sched_class.cpp 一
并链入。

自检 RunSchedulerSelfCheck 走完整真件路径：TaskBuilder 建内核任务
schedprobe，install_class(DefaultFifoClass) 加 InstallKernelSwitchSink，
seat 入队，run_until_done 从 main 经 enter_from_main 切进任务，任务体
exit_current 退休，reclaim 归还栈页与 Task 块，控制回到 main。打印
sched roundtrip ok 挂进 arm_boot 期望。preemption 保持关闭，探针自退休。

至此调度器上提只剩最后一跳：user_mode.cpp 的迷你调度（g_tasks 四槽与
c_schedule_from_user）换到这些真件上，用户任务的 trap frame 与通用 Task 的
CpuContext 对接。

## LCDIF 点亮：第一块屏幕

QEMU 11 的 mcimx6ul-evk 真的模拟了 i.MX6UL LCDIF（hw/display/
imx6ul_lcdif.c），但生成的 dtb 里没有它的节点，bring-up 先硬编码
0x021C8000。存在性靠写读回分辨：unimplemented 桩读零，真模型保持写入。

模型只答一张小脸：TRANSFER_COUNT(0x30) 定宽高，CTRL(0x00) 的 RUN 位与
WORD_LENGTH=3（读作 XRGB8888），NEXT_BUF(0x50) 的写入就是整帧触发。驱动
lcdif.cpp 恰好只编这张脸——320x240 一帧 order-9 页块，首图红条绿场，
顺序几何→RUN→触发。screendump 出 320x240 PPM 首像素 00 00 ff，像素真
落了屏。lcdif 320x240 fb 挂进 arm_boot 期望。

到 GUI 的路还差：devfs 的 /dev/fb0 座位（把这块帧缓冲交给用户态 vesper
桌面）、event 键鼠、vesper 本体在 armv7 的交叉编译。

## /dev/fb0 坐进 armv7，serial 与 console 收编中立

审计 x86 的 fb 链发现整条都是中立的：Framebuffer 单例吃 boot 的
FramebufferInfo，devfs 的 fb0 节点只是一组 DeviceOps。armv7 缺的不是中立
层，是喂进去的信息——user_fs 现在把 LCDIF 的 320x240 XRGB8888 折成一份
FramebufferInfo 交给单例，经 DeviceModel 绑进真 devfs，挂上 /dev，探针
resolve /dev/fb0 打印 dev fb0 320x240。

追缺口时顺手收编了两条双实现。serial：8250 语法从 serial.cpp 搬进
x86_64/platform/serial.hpp（platform 头，serial 进
CINUX_PLATFORM_HEADERS），armv7 侧同一个头转发 i.MX UART1，serial.cpp
变成纯转发壳，armv7 的 UART 补了 UartDrain（UTS1 TXEMPTY）。console：
armv7/console.cpp 退役删除，中立 console.cpp 顶上——它本来就是
SerialPutChar 加惰性 TextConsole，Serial 收编后 UART 输出一字不差，
arm_boot 期望全数照旧。字体 blob 用 arm 格式 objcopy 复刻 x86 规则。

至此 GUI 内核侧只差输入：event0/event1 的键鼠后端与 pty/console 节点还
坐在 x86 的键盘鼠标驱动上，那是下一块中立审计。

## event0 落座：UART 键盘喂中立事件流

审计 x86 输入链：EventStream 与 input_node 本就中立（语法直过），键盘只是
流的发布者。armv7 缺的是发布源。QEMU 的 imx6ul 有 chipidea USB 模型，但
USB HID 整链（host 控制器加设备枚举）是下一块整活，本轮先做诚实的近路：
uart_input.cpp 把串口 RX 折成 Linux 键码（字母表按标准 KEY_A 30/KEY_B 48
那张图，不是字母序），每次按键发布按下加抬起再关一条 SYN_REPORT。
PollUartKeyboard 挂在轮询路径，event1 的座位真实但安静——这块板没有指点
设备，宁可空流不造数据。

探针先把 device model 的 fb0 语义补了一课：devfs lookup 只给设备记录，
读之前必须 adopt_open（设备的 open 钩子把节点换成 per-open 的
InputOpen）。探针发布一条 KEY_A 报告，adopt 后单次读回 72 字节，逐字段校
验 wire 布局，打印 dev event0 72 bytes 挂进 arm_boot 期望。

## 中立 ELF loader 上车：guest 镜像装进 armv7 用户窗口

审计 x86 的 exec 链发现 loader 五件（loader/elf_layout/entry_picture/
exec_arguments/interpreter）本就中立，armv7 缺的只是三小件：abi port 头
（ELFCLASS32/EM_ARM/4 字 ABI 词）、page 的 UserLeafTerms 组合式、和一个统一
的 vfs 入口。第三件顺手立了中立面 kernel_vfs()——loader 不再抱 x86 的
FileWorld 单例，两端口各在自己的 vfs 坐骑旁定义这一个函数，一个机制一个
实现不变。

探针走真链：guest ELF 用 arm-none-eabi-gcc 链到 0x40000000（objcopy 成 blob
复刻 font 规则），写进 ramfs，经 vfs 读回，交 LoadElfIntoSpace 装进新
AddressSpace，打印 load guest entry 40000000 phnum 1 挂进期望。中途挖出
armv7 用户区的真边界：IsUserSlot 只认 0x40000000 那一个 L1 槽，bring-up 的
用户窗口就这一段，内核 identity 从 0x8000 起占着低区，guest 老老实实链进
窗口。将来用户区扩张时这个槽位约定要重新谈。

## guest 从 ELF 跑进用户态：第一趟真 exec

装载好的 guest 不再只是躺在 AddressSpace 里。guest 程序升级成真 syscall 客
户：write(1, 问候, 35) 再 exit(0)，svc 号走 r7，arm-none-eabi-gcc freestanding
编出 ELF32 链在用户窗口。user_fs 探针把装载产物移交堆（space、vma、entry、
一页栈在 mmap 基上），user_mode 的自检轮把 guest 坐进 task 4：同一张
g_tasks 表、同一套 seat_console 文件座位、同一个调度与 svc 分发面——它和内
置自检任务唯一区别是页面来自 ELF 而非内嵌镜像。guest 在用户态写出
[guest] hello from the loaded image，exit 后页账 64638 -> 64638 分毫不差。

这是 exec 链在 armv7 的第一趟端到端：交叉编译的 ELF、中立 loader、中立
vfs、中立 devfs/console、本端口 svc 面，全部串起来跑。x86 的 exec 与 armv7
的 exec 从此共享 loader 与 fs，剩下的差异只在切入和 syscall 分发的架构层。
小账：g_guest_ranges 全局未随任务回收（VmaStore 不持页，页账不受影响），
等调度器最后一跳换真 Scheduler 时一并处理。

## syscall 层中立体检：只差四个真 x86 件

为 exec 换血铺路，先审中立 syscall 层（kernel/syscall/，六十余件）在
armv7 交叉编译下的成色。结论：除 sys_arch_prctl（fs_base）、sys_mprotect
与 sys_munmap（x86 页表旗语）、sys_reboot（ACPI）四件真架构专属外，全部
语法通过——read/write/open/close/exec/fork/wait/futex/mmap/signals 全是
中立件，armv7 换血时直接接。

补了 armv7 缺的最后一块拼图：random port 头。板子没有硬件 RNG，QEMU 也
不模型一个，所以诚实写法是 entropy 池（中立 xorshift）种自 Generic Timer
计数器与链接地址，Word() 永远有答案，TryWord() 永远说没有——契约里写明
这是 bring-up 熵不是密码学源，x86 的 rdrand 路径在 armv7 上明确缺席而非
假装。sys_exec 顺手修了一处 ABI 敏感：gnu_stack_flags 从 unsigned int 改
std::uint32_t（uint32_t 在 arm 是 unsigned long，中立层必须定宽）。

## 用户进程坐上真调度器：x86 模型的 armv7 落点

审计 x86：Scheduler 的 Task 就是进程载体，切入用户态只是任务上下文的一次
跳转（init_thread 在任务体里 EnterUserMode），syscall 回到同一任务自己的内
核栈。armv7 的 ContextSwitch 早已为这个模型备好——SPSR 决定恢复进内核还是
用户世界，ldmia 恢复的 sp 就是任务自己的栈。缺的只是一次真跑。

新自检 RunSchedulerUserSelfCheck 补上这一跑：真 proc::Task，任务体在自己
的栈上 EnterUserModeWithReturn 进用户态（seat_prog.S 小程序：svc write 一句
问候、svc 0x1000 请求回内核）。写走的是中立 FileTable——current_open 先问
席位（任务自己的 files），再回落 g_tasks，一个入口两种坐骑。回内核用
ContextSwitch 的例外返回把 svc 处理态直接切回等待中的任务体，UserTrap 的
r4..spsr 十一个字恰好是一张 CpuContext 图像，不需要新汇编。

至此三个用户世界在同一 svc 面下共存：g_tasks 自检轮、loader 装的 guest、
真调度器上的有座用户进程。下一片把 g_tasks 的散件（wait/fork/mmap/brk）
逐一搬到中立 proc 件上，搬完拆旧轨。

## guest 搬上真调度器：旧轨第一次真正让位

guest 不再坐 g_tasks 的 task 4。席位机制泛化成一个共享任务体：SeatWorld 带
着 space/entry/栈顶进任务，任务体登自己的 FileTable、切根、切入用户态；回
来或退出都经 ContextSwitch 例外返回。svc 表的 exit 分支先问席位——有座客
在跑就把退出词记下、切回任务体、走调度器的 exit_current，旧轮转根本不被打
扰。

于是交叉编译的 guest 成了真正的进程：真 proc::Task、自己的文件表、自己的
世界、干净的退出。g_tasks 回归纯自检角色，guest 生命周期挪出后，
RunUserModeSelfCheck 与席位自检共用一个页账窗口（before 在 userfs 装载前、
after 在 guest 世界 teardown 后），64638 -> 64638 依旧分毫不差。

## guest 吃上中立 syscall 件：read 一段就绪

中立 SyscallFrame 是一块寄存器坐席表（名字来自第一港，坐席本身中立：号
码、调用者栈、返回地址、标志、六参）。armv7 的 frame_bridge 把 UserTrap 映
上这些坐席再映回，中立件因此不识港口。svc 表为席位世界接了 read/open/
close/lseek/getpid（HandleRead 等），guest 升级成真文件公民：open /motd、
read、write 回显 "hi there"、close、exit(0)，每步都是 x86 同款件。

loader 补中立件 preheat_anonymous：匿名段原本只登记 VMA，等缺页补页——
x86 有缺页员，armv7 没有。LoadWorld 加一个开关：x86 维持 false 按需补页，
armv7 置 true 在装载时就把零页补齐（起点对齐 file 尾页之后，不与
map_tail 重叠），一个 loader 服务两港。syscall 层残留的 6 处
FileWorld::self().resolve 顺手换 kernel_vfs().resolve；mount/sync 的
FileWorld 卷管理面留给下一片。

## 卷同步走挂载表：FileWorld::sync 退役

sync 的真义是"把持久卷冲干净"，卷都挂在 Vfs 挂载表上——面应该在那里。Vfs
加可选钩子 sync（FsOps 尾部声明，EraseFs 用 if constexpr (requires...) 只在
backend 说 sync 时填，ramfs/devfs 零改动），加 sync_all 遍历挂载表逐个冲。
sys_fsync/sys_sync/sys_reboot 全改问 kernel_vfs().sync_all()，FileWorld::sync
声明与定义一并删除——x86 单例又瘦一圈。

## tmpfs 挂载出走：FileWorld 又瘦一圈

mount_tmpfs/umount_tmpfs 的座位表（RamFs 卷 + MountLease + 前缀）原住在 x86
的 FileWorld 里，逻辑本身完全中立。搬进新中立件 kernel/fs/tmpfs_mount：自
持座位表与锁，两港经同一 syscall 面到达。FileWorld 的 TmpFsSeat、座位表、
mount_gate_ 一并删除，sys_mount 直接问中立件。

## 席位世界接中立缺页员：brk/mmap/munmap 全通

x86 的缺页承诺员（mm::demand::FillPage）本就是中立件，armv7 数据异常的修复
门（InstallFaultRepair）也早已就位——只差接线。席位任务 adopt 空间与承诺
表（task->space/task->vmas/task->user_root），SeatRepairPage 把 not-present
异常交给 FillPage 按 VMA 补页。为此拔了两颗钉子：

一是 demand.cpp/munmap 里 x86 专用的 entry->has(page::kPresent)，改走 walk
转发的 DescriptorPresent。二是 seed 面重复：sys_exec 与 init_thread 各抄一
段 heap/mmap 布局种子，提取 loader::SeedProcessLayout，armv7 的 guest 装载
也走同一面；armv7 布局常量原是 x86 照抄（3G 用户区），实际用户窗口只有
0x40000000 起 1MiB，收紧为 heap 带 0x40080000、mmap 顶 0x400F0000，滑动量
成为 arch 常量且中立件统一页对齐（歪的 ceiling 会发不对齐的承诺）。

svc 席位分派表接 kBrk/kMmap/kMunmap 中立件。guest 升级为内存公民：brk 问
基址再伸页、写读经真缺页补页、匿名 mmap 两次触碰补页、munmap 收回。途中
两处教训：run_seated_world 重置 SeatWorld 会把调用方捎带的 ranges 清掉；
heap 承诺从非对齐基址起，写页首等于写承诺之外。

席位空间生命周期改由任务 Shared 接管（析构自动 teardown），SeatWorld 的
owns_space 双轨删除退役。arm_boot 3/3，host 单测全绿，ledger 64634 平衡。

## preheat 退役：armv7 转纯按需装载

缺页员接线后，LoadWorld 的 preheat_anonymous 急填零页成为第二条实现同一
机制的轨道，按原则删除。guest 的 bss 从此和 x86 一样按需补零。

连带暴露一处真分歧：armv7 的 uaccess 是先查映射再搬运（内核直写用户地址
会变成无法修复的内核异常），bss 页不在表里时 read 直接 -EFAULT，偶发
"motd read failed"。修法不是绕开，而是让 uaccess 站到缺页员后面——
MoveUserBytes 的实现从头件挪进 user_access.cpp，查到缺失页时先请中立
FillPage 补页再搬运。旧轨任务没有承诺表，保持原拒绝语义不变。

arm_boot 5/5，host 单测全绿。

## guest 全局指针单向移交

席位任务接管空间与承诺表后，user_fs 的 g_guest_space/g_guest_ranges 全局
仍在别名化任务 Shared 将要释放的对象。take_* 改为单向移交（取走即置
空），顺带删掉已无调用者的 guest_seated 残面。

## 设备树装配中立化：SeatDeviceTree

x86 的 FileWorld::init 里烧着一段每港都该走的装配次序——绑设备模型、注册
builtin/console/framebuffer/pty、落两个输入源。提取 fs::SeatDeviceTree，
两港同走一面：armv7 只贡献自己的两个事件流（UART 键盘 + 板指针），
顺带获得完整 /dev 树，ptmx 从此在 arm 车道上落座。

连带把 console_tty 的键源中立化：纪律类原本直连 PS/2 与 USB 键盘单例，
改为 ConsoleKeyTap 注入对——x86 在装配时装 PS/2+USB 轮询，armv7 只剩
串口。tty/pty 栈全部文件 armv7 交叉语法零错。

arm_boot 3/3，host 单测全绿。

## ioctl 中立化与 armv7 的 pty 往返

### 做了什么

- `kernel/syscall/sys_ioctl.cpp` 现在是纯中立的：socket 接口询问分支整块搬进 `kernel/net/socket.cpp` 的 `kOps.ioctl` 槽（`socket_if_ioctl` 适配器转调 `socket_if_ioctl_impl`），net 头文件不再出现在 sys_ioctl 里。
- HandleIoctl 的分派语义补齐：没有 span 的 ioctl 词不再一律 -ENOTTY，而是把 argument 原样传给 backend。说整个结构的后端（socket 的 ifconf/ifreq 族）自己持有用户拷贝，只认 stage 词的后端照旧对陌生词答 -ENOTTY。pty 的 TIOCGPTN/TIOCSPTLCK 已有 span，走 stage。
- `kernel/net/ifaces.cpp` 对 NetWorld 的硬绑改成注入面：`net::SetWireFactsSource(WireFactsSource)`，NetWorld::init 注入自家闭包。中立测试和没有网线的 lane 不再为了接口名册拖进整个网世界。
- tcp.cpp 两处 `std::min` 在 32 位 arm 上的类型收窄（kTcpMssBytes、kCeiling 差值）补 cast。
- armv7 链接面接上完整 net 栈（ifaces/net_world/loopback/arp/icmp/ipv4/net_stack/net_layout/local_socket/raw/socket/tcp/udp）加 `kernel/time/timer_table.cpp` 与 `kernel/syscall/sys_ioctl.cpp`，svc 表加 `kIoctl = 54`。
- 测试框架新增共享 `test/framework/console_host.cpp`（console::PutString 桩），test_net_socket 里的本地副本删除，test_syscall_face 补链 ifaces.cpp。
- guest 程序加 pty 往返探针：open /dev/ptmx，TIOCGPTN 问从端号，open /dev/pts/N，master 写 "ping\n"，slave canonical 读回。注意 canonical 行要换行才交出。
- run.cmake 期望更新：guest entry 40000000（新链接参数 -Ttext 0x40000000），新增 EXPECTED_PTY。

### 怎么验证的

- `./scripts/check-armv7.sh --repeat 3`：arm_boot 3/3，both lanes green。
- 全部 host 测试 0 failed，`ninja -C build` 零错误。
- 手动 QEMU 引导确认 guest 五行探针全过：hello、motd、brk、mmap、pty，随后 status 0 退出。

### 踩坑

- guest 的 assets/armv7_userprog.elf 是预编译产物，改 .c 后要手动用 arm-none-eabi-gcc 重链（-Ttext 0x40000000 -e guest_main），CMake 只负责把 elf 嵌进镜像。
- 拖 test_syscall_face 依赖链（net_world -> task_runtime -> arch 汇编）是死路，中立测试不能碰 arch 符号，正确解法是 ifaces 的注入面。
- 32 位 size_t 与 uint32_t 常量同传 std::min 会推导失败，arm 报错而 x86 因 64 位 size_t 恰好通过。

## seated write 中立化：stdio 走真 /dev/console

### 做了什么

- `kWrite` 加入 armv7 svc 中立表（`HandleWrite`），seated guest 的 write 不再走旧 answer 的 `UartPutChar` 直写路径。
- `FileWorld::seat_console_streams` 的逻辑提取为中立自由函数 `fs::SeatConsoleStreams(Vfs&, FileTable&)`，放在 `kernel/fs/device_tree.cpp`（device_tree 已是两 lane 共链的中立装配点）。FileWorld 方法转调它，armv7 的 `SeatProcessBody` 也转调它：seated 进程的 0/1/2 现在绑到 SeatDeviceTree 装好的 /dev/console tty 节点，与 x86 init 的 stdio 同一条机制。
- `kernel/syscall/sys_write.cpp` 链入 armv7 镜像。
- run.cmake 的 EXPECTED_SEATWRITE 从旧调试行 `svc write 40 bytes` 改为字面 `[seat] scheduler task writes from user m`（seat 探针 40 字节真输出）。

### 效果

- guest 五行探针（hello/motd/brk/mmap/pty）现在全部由真 console 设备吐出，字面内容与旧行一致，机制却从 UART 直写换成了 tty 线路。seated 世界的第二条输出路径删除。
- 旧 g_tasks 自检世界的 console face（erased UART face）保留，它随 g_tasks 整体退役，不再单独扩面。

### 踩坑

- file_world.cpp 不能整个链进 armv7（拖 Ahci 等 x86 驱动装配），中立函数应落在 device_tree 这个两 lane 共链的装配点。
- armv7 期望值是字面匹配，行为换路径后调试打印行会消失，期望要跟着改字面。

### 怎么验证的

- `./scripts/check-armv7.sh --repeat 3`：arm_boot 3/3，both lanes green。
- 全部 host 测试 0 failed，`ninja -C build` 零错误。

## fcntl 上中立表：非阻塞 stdin 探针

### 做了什么

- `kFcntl = 55`（ARM EABI 号）进 armv7 svc 中立表，`HandleFcntl` 本就全中立（FileTable + status flags），无需改动。`sys_fcntl.cpp` 链入 armv7 镜像。
- guest 新增非阻塞探针：`fcntl(0, F_SETFL, O_NONBLOCK)` 后空 stdin 的 read 立即答 -EAGAIN，不再有无输入时的阻塞风险。

### 细节

- console tty 的 FsOps 没有 try_read 槽，但 sys_read 的等待循环对 `WouldBlock + O_NONBLOCK` 组合同样立即转 EAGAIN，探针走的正是这条路径。

### 怎么验证的

- `./scripts/check-armv7.sh --repeat 3`：arm_boot 3/3，both lanes green（新增 EXPECTED_FCNTL）。
- 全部 host 测试 0 failed，`ninja -C build` 零错误。

## fstat 上中立表：guest 读出 motd 的形状

### 做了什么

- `kFstat = 108` 进 armv7 svc 中立表。审计确认 `HandleFstat` 全中立（LiveSlot + FsOps.stat + uaccess），零改动，`sys_fstat.cpp` 链入 arm 镜像。
- guest 新增探针：fstat /motd 验证 mode 0100644（ramfs 的 kFsFileMode）与 size 9（"hi there\n"），guest 侧声明与 SyscallStat 同构的记录。

### 踩坑

- run.cmake 有期望名查重（同名 -D 只留最后一个会静默丢期望），guest 的 fstat 期望撞上旧自检世界的 EXPECTED_FSTAT，改名 EXPECTED_GUESTSTAT。
- CMakeLists 里替换路径时前缀截断产生 kernel/sys_fcntl.cpp 这类坏路径，cmake 直接拒绝配置，修回 kernel/syscall/ 全路径。

### 怎么验证的

- `./scripts/check-armv7.sh --repeat 3`：arm_boot 3/3，both lanes green（新增 EXPECTED_GUESTSTAT）。
- 全部 host 测试 0 failed，`ninja -C build` 零错误。

## getdents 中立化与旧世界双释放修复

### getdents 切片
- 审计 x86 路径后确认 `kernel/syscall/sys_getdents.cpp` 本就纯中立（LiveSlot 取活口、ops->read_dir 遍历、uaccess 写 dirent64 记录：ino@0、off@8、len@16(u16)、type@18、name@19，记录 8 字节对齐），无需新造任何机制，直接复用。
- armv7 侧接线：`syscall_numbers.hpp` 增 `kGetdents = 217`，svc 中立表加行分发 `HandleGetdents`，CMake 显式源列表加 `kernel/syscall/sys_getdents.cpp`。
- guest 探针最终形态：open("/") 后 syscall 217 拉 512 字节缓冲，按 len 遍历记录，断言 5 条且含 hello，输出 `[guest] getdents ok, root holds five entries with hello`，run.cmake 增 `EXPECTED_GETDENTS`。

### 旧世界 open 双释放（本切片发现的真 bug）
- 现象：ramfs 根最早 seed 的 hello 节点在 g_tasks 自检期间名字被踩成 `20 e2 00 64`，getdents 第 5 条乱码、lookup("/hello") 找不到。
- 二分链：seed 时完好 → staging 后完好 → task 世界启动前完好 → task 0 fork 掩码阻塞时已坏 → 精确到 task 3 的 open("/scratch") 的 `FileTable::alloc` 调用窗口。
- 根因：`user_mode.cpp` 旧 open 处理器失败路径双释放。1696 行 alloc 返回后无条件 release（消 lookup_held 的 +1，正确），1700 行 !kFd.ok() 时再 release 一次。但 `FileTable::alloc` 在表满时于锁内 vacant_locked 判定即返回，先于 new OpenFile 与 ops->retain，失败时不持有任何引用。多放的一次让 hello 的计数（树边 1 + lookup_held 1）归零，RamNode::release_ref 执行 delete，父目录 child 链悬垂。之后 alloc 的 52 字节 OpenFile 复用该内存，构造函数字段写入覆盖 name[0..3]。
- 触发场景：fd 上限探针（8 槽表满打出 "open refused, no free slot"）时 open 的正是 /hello。
- 修复：删掉失败分支里的第二次 release，注释说明 alloc 拒绝发生在自身锁内且先于任何 retain。中立层（file_table/open_file/Shared/ramfs）对照审计无 bug，x86 的 sys_open 配对正确，不动。
- gdb watchpoint 未命中的解释：写入者是复用者（OpenFile 构造），而非直接针对旧地址的踩踏，且 %X 对指针有符号扩展显示问题（用 %u 才准）。

### 页数断言语义修正
- 修复后 hello 活着，单轮自检页数 64573 -> 64571（堆增长驻留，分配器不还页），旧的 before==after 断言误报泄漏。
- kmain 改为两轮运行：第一轮的堆驻留不视为泄漏，断言第二轮前后页数相等。实测 64573 -> 64566 -> 64566，round trip ok。

### 验证
- `./scripts/check-armv7.sh --repeat 3` both lanes green。
- 全部 84 个 host 测试 0 failed。
- `ninja -C build` 干净。

## stdin 行读取打通

### 审计结论
- 座位 guest 的 stdin 已是 /dev/console 的真 tty：SeatConsoleStreams 装的是 DevFs 的 FsOps,read 桥到 DeviceOps::read(console_node.cpp),行规 Tty::read_line 直接经 wiring take_key 拉 platform::SerialTryGetChar(armv7 的 platform/serial.hpp 转发 UartReceiveReady/UartReadChar)。
- 阻塞语义链完整:HandleRead 对阻塞 fd 在 kWouldBlock 上 yield 重试,每轮 read_line 自己尝试 take_key,无需输入泵。非阻塞路径(F_SETFL O_NONBLOCK)因 ops->try_read 为空走 read,kWouldBlock 映射 EAGAIN,先前 fcntl 探针已验证。
- 结论:不需要写任何新实现,只缺一个端到端探针把字节送进 UART。

### 注入时序(三条窗口规则)
- 0 秒注入的字节在内核 UartInit 之前到达,QEMU 的 imx-serial 模型直接丢弃。
- 注入落在旧 g_tasks 世界 read pending 窗口内会被其 tick 唤醒的 DrainConsole 吃掉。
- 注入晚于 read 段即可:guest 的阻塞读无限等,FIFO 里的字节照常读出(tty echo 会在串口回显该行)。
- boot_check.cmake 增加 STDIN_FILE/STDIN_DELAY 分支:(sleep N && cat 文件) | qemu。assets/armv7_stdin.txt 内容 cinux\n,延迟 6 秒,窗口 CINUX_ARM_RUN_SECONDS 5 -> 20。

### guest 探针
- 阻塞 read(0) 读一行,断言 6 字节 "cinux\n",输出 `[guest] stdin ok, line read back`,EXPECT_STDIN 入 run.cmake。放在 fcntl 之前:先消费注入行,后面的空读 EAGAIN 依然成立。

### 验证
- `./scripts/check-armv7.sh --repeat 3` both lanes green。
- 84 个 host 测试 0 failed,`ninja -C build` 干净。

## armv7 写异常修复统一到单一接缝

### 审计
- 旧 g_tasks 世界的 RepairUserPage 里混着两段几乎相同的复制逻辑: 共享帧离开账本(cow copy)与私有只读页换可写帧(ro copy), 仅差旧帧是否还有别的持有者。
- 座位世界的 SeatRepairPage 只接中立需求分页器, 写异常无人修——fork 迁移前必须补上。

### 提取
- kernel/arch/armv7/cow_repair.cpp: mm::cow::RepairWriteFault(root, address) 一个函数吞并两分支: 帧在账本里 >= 2 份为共享, 否则查 UserPageIsWritable 拒绝非本门的异常; 复制帧、重挂可写叶、ReleaseFrame 旧帧, 日志按共享/私有分别报 cow copy/ro copy。
- 旧世界 RepairUserPage 与座位 SeatRepairPage 都改为调它, 复制逻辑只剩一份。ReleaseFrame 补进 cow_ledger.hpp 声明。

### 座位侧验证
- 座位代码页本就映射为可写(默认 leaf bits), 改为 UserLeafTerms(false, true) 只读可执行。
- guest 探针写自己代码页尾垫料 0x40000FF0: 触发座位世界首个真实 COW 修复, 回读断言, 输出 `[guest] cow seat ok, code write repaired`, EXPECTED_SEATCOW/EXPECTED_SEATROCOPY 入期望表。
- 内核日志出现座位世界的 `ro copy at 40000000`, 与旧世界的 40030000 互不干扰。

### 验证
- `./scripts/check-armv7.sh --repeat 3` both lanes green。
- 84 个 host 测试 0 failed,`ninja -C build` 干净。

## 静默失败补回声

`open` 没空槽、`lseek` 参数不对、`read` 缓冲不对，这三条原先都只返回错误码、
一行不打；上一轮那次"可写文件看起来没执行"就是第一类。现在各补一行，并且
在自检里把后两类也走一遍：对文件描述符 lseek 到负偏移、读一个内核地址，用户
侧拿到 -22 与 -14 之后分别报 lseek bad ok 与 read bad ok。

## 一次偶发与它的修正

连跑十次时有一次失败：断言 `wake a waiter of 1` 没有出现。看留下的日志，
0 号任务确实等到了 1 号的退出码，只是它当时没有被挂起：等待在 1 号结束之前
就按超时回来了，等结束之后走的是"目标已完成"的快路径，自然不会打印唤醒。
换句话说，这条断言依赖两条任务的交错顺序，不是稳定事实。

两处改动：把按目标回收的那几处等待从默认 20 tick 改成 200 tick，让等待者
在目标结束前一直挂着；"按目标唤醒"的断言改用 fork 里父任务等子任务那一条
（子任务什么时候退出与父任务是否在等没有竞态）。改完连跑十二次零失败。

## 稳定性

`scripts/check-armv7.sh --repeat N` 把两条件都过一遍，其中 `arm_boot` 连跑
N 次；失败时脚本会指出现成的留档日志。默认一次。

最近一次复核（17 个号、104 条断言）连跑五遍，五遍全过。

同一份镜像连着跑五遍 `arm_boot`，五遍全过，每遍都在跑满 5 秒的观察窗口
（5.01 到 5.02 秒），没有一遍提前收工或超时。已知的敏感项只有两处：`read`
的超时依赖 tick，机器负载极高时可能变慢；串口灌输入那条分支没有覆盖。

## 系统调用的覆盖表

号与语义在上面，这张表记的是"哪条自检真的走到了它"：

| 号 | 语义 | 覆盖它的自检 |
| --- | --- | --- |
| 1 | exit | `task 0 exited with status 7` |
| 2 | fork | 连 fork 两次：`fork 0 -> 4`、`fork 0 -> 5`、`child one ok`、`child two ok`、两条都以 3 退出；写时复制：两次 `cow copy at 40040000`、`cow after two forks`；继承：`fd inherited ok`（父任务关掉的 2 号在子任务里也是关的）、`brk inherited ok`（子任务的断点接着父任务的 0x4000E000）；槽位复用：子任务退出后再 fork 回到 4 号；孤儿：父任务先退之后子任务的 ppid 仍读作 0（`orphan ok`）；处理函数继承：父任务登记过处理函数，复用的子任务踩 `0x51000010` 也进处理函数（`fault handled at 51000010`、`handler inherited ok`、以 9 退出）；占满八条后第五次被拒（`fork refused, no free slot`、`no slot ok`），四条停着的子任务在父任务结束后各自退出 |
| 3 | read | `read timed out`（有数据那条未覆盖） |
| 4 | write | `svc write 22 bytes`、两种拒绝 |
| 6 | close | `close 2`、`ebadf ok`；文件槽回收：`close 3`、`reopen ok`（关掉后再开拿回同一编号） |
| 19 | lseek | `lseek 3 -> 0`、用户侧 `lseek ok`（读三字节、回零、再读六字节）；同一文件开两次各读各的（`multi open ok`）；开满 3 到 7 之后再开回 ENOMEM（`full ok`） |
| 20 | getpid | `getpid 3`、`pid N ok` |
| 64 | getppid | `getppid 0`、`ppid ok` |
| 78 | getticks | `getticks 3`、睡三个 tick 之后变大（`ticks ok`） |
| 5 | open | 内建只读文件按名字查表：`open hello as 3`、`file ok`，`open motd as`、`motd ok`，`open readme as`、`readme ok`；不存在的名字回 ENOENT（`enoent ok`）；可写文件：`open scratch as`、`file write 6`、`writefile ok`；只读文件不许写：`write 3 refused, read-only`、用户侧 `ro write ok`；末尾追加：`append ok`；跨任务共用：3 号任务轮询读到 0 号写下的同一段内容（`shared ok`） |
| 45 | brk | `brk -> 4000E000`、`brk ok` |
| 108 | fstat | 可写：`fstat 3 size 8 writable 1`、`fstat ok`；只读：`size 6 writable 0`、`fstat ro ok`；坏地址 EFAULT（`fstat bad ok`）、坏描述符 EBADF（`fstat 9 refused`、`fd stat bad`） |
| 90 | mmap | 单调分配、地址提示、只读、`flags 2 refused` |
| 91 | munmap | `munmap 40020000 pages 1` |
| 114 | wait | `blocks on`、`wait 1 -> DEAD`、`wait 2 -> 9`、用户侧 `status ok` |
| 67 | signal | `fault handler 40001008 for task 2` |
| 158 | sched_yield | `yield ok` |
| 162 | sleep | `sleeps 3 ticks`、`woke from sleep`、`slept` |
| 未登记 | 未知号 | `svc FFFF unknown`、`unknown refused` |

故障处理函数：任务可以给 SIGSEGV 登记一个用户态地址（`signal` 只认 11 与
非零处理函数）。用户态故障时，若登记过就把陷入帧改成"从处理函数继续"，
参数是故障地址与状态；没登记就照旧被收掉。自检里 2 号任务登记、踩
`0x51000000`、处理函数报 `handler ok` 并以 9 退出；1 号任务没登记，
`user fault caught` 与 `task 1 faulted` 照旧。

缺口只有一处：`read` 的"数据到了"分支。灌输入试过两种方式都不通，见上一节。

## 端口转发

中立头 `kernel/arch/<名字>.hpp` 不含 ISA 路径，它引用
`cinux/port/<名字>.hpp`。这个文件由根构建按 `CINUX_ARCHITECTURE` 生成，
内容是 include 当前端口的实现。x86_64 与 armv7 因此可以共存，换端口不改
共享文件。

armv7 目前给出了这些中立头的实现：context、halt、instructions、irq_guard、
isr、kernel_stack、page、page_entry、page_walk_shape、per_cpu、
per_cpu_config、space_control、space_layout、user_entry。其余等编译到再补。

平台的七条走同一套生成方式：`kernel/platform/<名字>.hpp` 只 include
`cinux/platform/<名字>.hpp`，后者指向 `kernel/arch/<isa>/platform/<名字>.hpp`。
armv7 的 bus 与 ps2 是故意留空的聚合头，因为这块板子上没有这两种设施；
firmware 指向设备树，clock、interrupt、io 分别包住定时器、GIC 与串口。

## 还没接的

现在这一版能跑通 17 个号、8 条任务槽、内建文件、匿名映射与一个迷你调度器。往下每一块
缺口要接进来，前提不一样，逐条列清楚：

| 缺口 | 最小前提 | 现在卡在哪 |
| --- | --- | --- |
| 文件系统 | 一块存储（内存盘或块设备）、inode 与文件表、描述符表从位图升级成"种类加偏移"的数组 | 没有存储驱动；`Task` 结构还带着 `fs::FileTable` 的构造与析构，一动就牵出来 |
| 通用调度器 | 把 `fs::FileTable`、`proc::SendSignal`、`futex::ClearTidWord` 一起搬进来；写一份 `SwitchSink` 的端口实现（栈顶、地址寄存器、三参数切换这三处已经抽好） | 三块一进来又会牵出文件系统、信号与 futex，环环相扣 |
| 网络 | 与机型匹配的网卡模型、GIC 上对应的中断号、一次性的 DMA 缓冲 | 网卡与中断路径都还没碰 |
| 用户态信号 | 现有的 SIGSEGV 处理函数只是改帧跳转，没有 `sigreturn` | 处理函数必须自己 `exit`，返回无处可去 |
| 文件映射 mmap | 文件表与页缓存 | 与第一条同根 |

接入顺序上，先做内存盘与文件表，通用调度器才有可能搬（它依赖文件表），
网络可以独立并行。

