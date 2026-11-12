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
| 断言 | arm_boot 100 条期望，全按字面量比对；同名 `-D` 在配置期直接报错 |
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
| 6 | close | `close 2`、`ebadf ok` |
| 19 | lseek | `lseek 3 -> 0`、用户侧 `lseek ok`（读三字节、回零、再读六字节）；同一文件开两次各读各的（`multi open ok`）；开满 3 到 7 之后再开回 ENOMEM（`full ok`） |
| 20 | getpid | `getpid 3`、`pid N ok` |
| 64 | getppid | `getppid 0`、`ppid ok` |
| 78 | getticks | `getticks 3`、睡三个 tick 之后变大（`ticks ok`） |
| 5 | open | 内建只读文件按名字查表：`open hello as 3`、`file ok`，`open motd as`、`motd ok`；不存在的名字回 ENOENT（`enoent ok`）；可写文件：`open scratch as`、`file write 6`、`writefile ok`；只读文件不许写：`write 3 refused, read-only`、用户侧 `ro write ok` |
| 45 | brk | `brk -> 4000E000`、`brk ok` |
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

现在这一版能跑通 15 个号、8 条任务槽、匿名映射与一个迷你调度器。往下每一块
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

