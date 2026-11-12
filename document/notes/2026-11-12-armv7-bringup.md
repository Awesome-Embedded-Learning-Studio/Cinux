# armv7 端口骨架

状态：能启动、能跑用户态、能按时间片抢占（2026-11-12）。这份记这一版活着
的东西、两条陷阱帧路径的分工、以及踩过的几处，方便后面接着做。

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
| 描述符 | `close 2`、`svc write refused -9 at`、用户侧 `ebadf ok` |
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
21. 测试参数里带空格的 `-D` 值没加引号，CMake 按空格把它切成多个参数，真正
    传进去的只有第一个词。于是很多断言长期只查了开头一个词。修法是整体加
    引号；顺便逐条核对：三十四条期望在输出里都能按整句找到，也都有对应的
    检查分支，其中一条早就不存在的 `svc write matched` 换成现在的
    `svc write`。
22. 断言用 `MATCHES` 比对，期望串因此被当成正则。`mmap` 那行写成
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

## 系统调用的覆盖表

号与语义在上面，这张表记的是"哪条自检真的走到了它"：

| 号 | 语义 | 覆盖它的自检 |
| --- | --- | --- |
| 1 | exit | `task 0 exited with status 7` |
| 3 | read | `read timed out`（有数据那条未覆盖） |
| 4 | write | `svc write 22 bytes`、两种拒绝 |
| 6 | close | `close 2`、`ebadf ok` |
| 20 | getpid | `getpid 3`、`pid N ok` |
| 45 | brk | `brk -> 4000E000`、`brk ok` |
| 90 | mmap | 单调分配、地址提示、只读、`flags 2 refused` |
| 91 | munmap | `munmap 40020000 pages 1` |
| 114 | wait | `blocks on`、`wait N -> DEAD`、`read timed out` 同族的超时 |
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

- 系统调用只有 exit、read、write、sched_yield 四条，read 只认 fd 0、
  write 只认 1 与 2，还没有文件描述符表。
- 克隆只在自检与四条任务里用：子树能克隆、写时复制能各写各的，任务结束也
  会把空间还回去。等待能阻塞、按位图等、按目标唤醒、能超时，sleep 也能用。
  用户堆只有 brk 的只增窗口；mmap 只做匿名映射：能收地址提示、能标只读
  （flags 的 bit0，别的取值一律 EINVAL）、能用 munmap 还页，但没有文件映射
  与共享映射；描述符只有 0 到 2 三个，没有文件系统。写时复制有两种：共享
  帧被写时复制成各自私有（`cow copy`），只读私有页被写时复制成可写
  （`ro copy`）。没有进程、会话与信号，调度也还是端口里的迷你
  实现，没有接上 `kernel/proc/scheduler.hpp` 的通用调度器（见上一节）。
- 内核态中断在用户阶段之后重新开中断的那条路径已经验过：开中断之后跑两拍
  再触发缺页探针，串口里看得到内核态被服务的那一拍。
- 内核调度器没有接进来。现在的两条用户任务是自检里的小调度，帧、根表与
  时间片都能用，但任务结构、生命周期与阻塞还没长出来。
- `abi`、`random`、`smp_control` 这些中立头还没有 armv7 版。
