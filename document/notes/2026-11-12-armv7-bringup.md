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
  `svc.cpp` 是号表（exit 1、read 3、write 4、sched_yield 158），
  `user_entry.S` 做进门与恢复。
- 用户指针：`user_access.hpp` 先验整段在用户顶以下、不溢出，再逐页看活动表
  里是否对用户可读；`write` 与 `read` 都走它，坏指针返回 -1 而不是把内核
  地址当用户缓冲。
- 地址空间的生与死：`KernelTables::take_root` 与 `release_root` 由端口给
  尺寸与对齐，`AddressSpace::teardown` 按 `IsUserSlot` 找用户槽并把帧、
  表页、根表还回去；自检验的是拆完空闲页数回到原点。
- 调度：两条用户任务，各自的空间、用户栈与帧；时间片一个 tick，从用户态
  来的中断里递减，到零就让出。另有 `task_switch.cpp` 的两条内核侧任务轮转，
  是最早那次预演，留着当上下文切换的独立自检。
- 平台缝：`kernel/platform/<名字>.hpp` 只 include `cinux/platform/<名字>.hpp`，
  这个文件由根构建按 `CINUX_ARCHITECTURE` 生成，指向该端口的聚合头。七条
  中立头（bus、clock、firmware、interrupt、io、ps2）都走这条缝。

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
| 两条内核任务轮转 | `two tasks ok` |
| 两条用户任务与抢占 | `preempt to task`、`task 0 exited with status 7`、`task 1 faulted` |
| 坏指针 | `svc write refused 80000000 + 8` |
| 读 | `svc read 0 bytes` |
| 用户态往返 | `user mode round trip ok` |
| 故意缺页 | `data abort at 51000000 status 805 code 2` |

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
- 帧的回收只支持独占：没有 COW 的账本，`cow::ReleaseFrame` 直接把页还给
  PMM。要 fork 或共享映射就得先补这本账。
- 内核调度器没有接进来。现在的两条用户任务是自检里的小调度，帧、根表与
  时间片都能用，但任务结构、生命周期与阻塞还没长出来。
- `abi`、`random`、`smp_control` 这些中立头还没有 armv7 版。
