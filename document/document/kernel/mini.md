# 小内核

## 功能概述

这一页记的是一次退场。小内核（源码注释里的 Cinux Mini Kernel，住在旧树的 `kernel/mini/` 目录）在 2026 年 10 月 31 日随重构迁移整体删除，现在的源码树里已经没有这个目录了。它在旧引导链里站的是中段：磁盘的 LBA 16（第 16 号逻辑扇区）是它的座位，旧版 stage2 把它从那儿装进了内存，它把串口、物理内存、中断的装备备齐了，再从磁盘把大内核读进 `0x1000000`（16 MB）的暂存区，把段铺好了、入口一跳就交了棒。现在这段活换了干法，由 [引导链](../boot.md) 一页所写的 MBR（主引导记录）、第二程的 stage2 与内核入口直接接力，磁盘镜像里不再有小内核加载大内核的一层了。我们把它的来历、职责与退场，按 git 的历史和现存的承接文件记在下面。

旧源码的引用都带提交号，想对照原文的话，你可以随时用 `git show 49ddfb0^:kernel/mini/main.cpp` 这样的命令调出来。现行机制的细节不在本页展开，走读的事归各承接页。

## 生卒：从迁入到删除

来历在 git 的历史里记得清楚，我们把两个日期从提交记录里对了出来。2026 年 5 月 25 日的 `cde30d1` 把它连着 45 个文件从旧仓库搬了进来，提交说明写的是 `migrate from old Cinux`。源码注释里还带着那个年代的里程碑编号，`main.cpp` 的文件头写着 Milestone 009，副题讲的是从磁盘加载大内核然后跳过去。此后的五个多月里，它跟着旧仓库的节奏走：`63fb155` 清过 big 与 mini 两边的 `int $3` 调试残留，`dd22549` 修过 mini_kernel_test 的构建装配，`test/` 子目录里躺着的则是它自己的测试，`test_pmm`、`test_ata`、`test_elf_loader` 我们都点过名。它们的编法是把测试编成一个专门的测试内核镜像，放到 QEMU 里开机跑（提交记录里的 run-kernel-test 就是干这件事的），跟今天开发机上跑的单元测试是两回事。

退场发生在 2026 年 10 月 31 日的 `49ddfb0`，提交说明写的是 `feat: Migrate the refator version of Cinux` 这一句，除此之外一个字的动机都没有。这笔提交把 `kernel/mini` 的 45 个文件全数删掉了，数目与迁入时的 45 正好对上，中途没有过新增的文件，目录从此在源码树里消失了。旧文件的原文还调得出来，你可以用 `git show 49ddfb0^:kernel/mini/main.cpp` 看到它当年收尾的样子。

## 它管过什么，职责归了谁

我们把 `kernel/mini` 的文件清单按职责分开，一摊一摊地对过去：每小节记的是它当年干的活，末尾接的则是它如今的去处。下面点到的旧路径都在当年的 `kernel/mini/` 之下，前缀就不每一处重复了。

### 串口与打印

`kernel/mini/driver/serial` 与 `kernel/mini/lib/kprintf` 是它开口说话的两件装备。我们把 `kprintf.h` 注释里的格式说明符读一遍：整数的 %d 与 %u，十六进制的 %x 与 %X，字符串与指针的 %s 与 %p，字符与百分号的 %c 与 %%，外加二进制的 %b。宽度写法则是 %Nd 与 %0Nd 这么两种，右对齐的空格与零都补得上。现在的去处分两半。引导世界的播报走 `boot/early/console.hpp` 与 `boot/early/print.hpp`，COM1 配的是 115200 8N1，这样安排的道理在 [引导链](../boot.md) 一页里写过，内核侧的串口则由 `kernel/driver/serial` 接管，`kernel/init/init_sequence.cpp` 的序列里，serial 站的是头一步。

### 物理内存

`mm/pmm.cpp` 里住的是一张静态位图分配器（`s_bitmap` 数组），分配的时候扫位图，找的是头一个空下来的位。`init(const void*)` 收到的是引导递来的裸指针，它就是解析的入口，内存清单顺着它解析了出来，我们按它的判断读：类型为 1 的可用区才收，低 1 MB 的区整段滤掉，源码注释写明了缘由，那一段是留给引导程序的，余下的按页对齐标成了可用。现在的收集与交接换了主人：`boot/bios.cpp` 的 `CollectMemoryMap` 循环问 E820（BIOS 报内存底细的接口，一条记录说一段内存的起点、长度与用途），`boot/e820/e820.hpp` 管条目的归档，清单装进 `kernel/boot/boot_info.hpp` 的 BootInfo 递给内核，`kernel/mm` 的 PMM 再从交接记录把空闲位图搭起来。E820 收集的来龙去脉，我们在 [引导链](../boot.md) 一页里写过，位图怎么搭起来的则归 [物理内存管理](mm/pmm.md) 一页。

### 中断、GDT 与页表

`arch/x86_64/` 目录里住的主要是这么几样：`gdt` 与 `idt` 各是一对 .hpp 加 .cpp，外加的是 `interrupts.S` 与 `paging.hpp`。IDT（管异常与中断的中断描述符表）是个简化过的版本，`idt.cpp` 的文件头注释写明，配上的只有 #BP(3) 与 #PF(14) 两个向量，断点与页错误各配了一个桩，桩的 C 侧处理函数住在 `exception_handlers.cpp`（它的文件头写明了这件事），干的是把寄存器打上串口的活，打完了让程序接着跑。剩下的两样里，`boot.S` 管的是入口，`crt_stub.cpp` 留到下一节的 C++ 运行时里讲。`paging.hpp` 搭的是两路映射，`linker.ld` 的注释说了，身份映射与高半区映射指的是同一批物理页。现在的接法变了：引导世界不开中断、也不装 IDT，stage2 进门就把中断关了。GDT（全局描述符表）、TSS（任务状态段）、IDT 与异常桩的装配，全数落在了内核入口的 `kernel/arch/x86_64/entry.S` 与 `entry.cpp` 两个文件里。交接期的页表归 `boot/lm/page_tables.cpp` 的 `BuildHandoffDoors`，内核接手后的地址空间则归 `kernel/mm`。这几处的走读，我们在 [引导链](../boot.md) 一页里都能读到。

### 磁盘与加载

`driver/ata` 是台管 ATA 硬盘的 PIO（程序化输入输出的缩写，CPU 自己轮询端口搬数据、不等中断）的只读驱动。`ata.hpp` 的文件头注释交代得全：走的是主通道的 `0x1F0` 到 `0x1F7` 加 `0x3F6`，DMA 是不碰的，认的寻址有 LBA28 与 LBA48 两种。它给自己在链条里的定位也写在注释里，说的是这个小内核就靠它把大内核从磁盘上加载起来。`elf_loader` 管的是 ELF64 的解析，也就是把可执行文件的格式读懂，`big_kernel_loader` 把大内核从 LBA 848 起的地方（常量名是 `BIG_KERNEL_LBA`，位置排在 mini 自己的保留扇区之后）读进 `0x1000000` 的暂存区，把 PT_LOAD 段（ELF 里标着装到哪个地址的段类型）铺好了，再跳进它的入口。`main.cpp` 的收尾我们读过：入口地址是零的话就报错停机，打出的则是 `[MINI] Jumping to big kernel at %p...` 一行报话，跟着一次间接跳转就交接了出去。

现在这套活的接法换了，我们把新链的做法对一遍：磁盘的读取，走的是 BIOS 的 INT 0x13 扩展读，内核里不再有自己轮询端口的加载驱动了。ELF 的解析也从运行期退到了构建期，`kernel/arch/x86_64/kernel.ld` 把装载地址与入口折成了符号，同目录的 `image_header_data.cpp` 再把符号折成 40 字节的镜像头，内核以平铺二进制的形态放在 LBA 32，stage2 按九条的清单验收，过了关才经 ferry（引导借保护模式往返搬内核的通道）搬到 `0x200000`。整段的走读还是在 [引导链](../boot.md) 一页里。

### C++ 运行时

`arch/x86_64/crt_stub.cpp` 顶的是 freestanding 环境的边角，标准库是不靠的，运行时该有的家当都归它自己备。头注释里交代了缘由，`-nostdlib` 加 `-ffreestanding` 的环境下，libstdc++ 备的那些函数得自己来：`__cxa_pure_virtual` 与 `__stack_chk_fail` 各是一段停机的循环，一个管的是虚函数调用走了空，另一个管的是栈保护探到了溢出，`__cxa_atexit` 给的则是空实现。`linker.ld` 给 `.init_array` 留了段，全局构造走的就是这里。这套桩的下场，放到现在是不一样的，我们分开看。`__cxa_pure_virtual` 与 `__cxa_atexit` 是没有对位物的，我们搜过：引导世界的 C++ 收得很小，控制台干脆做成了头文件内联的样子，`console.hpp` 的注释讲过这样安排的道理，内核那边走的是零构造的路子，`kernel/proc/scheduler.hpp` 的类注释里写着一句英文，“Constant-initialized state only, per the zero-construction rule”，说的就是状态全部走常量初始化，全局构造根本就发生不了，这两枚桩也就用不上了。`__stack_chk_fail` 则有人接了班，而且接得更严：哨字 `__stack_chk_guard` 住进了 `kernel/arch/x86_64/entry.cpp`，开机时拿时钟的周期数重新播种，函数本体落在了 `kernel/debug/panic.cpp` 里，编译还开着 `-fstack-protector-strong`（栈保护的加强档），探到溢出的时候，打出的就是 `[panic] stack canary smashed, halting`，然后就停机了，不再是一段干等的停机循环。

## 为什么退场

`49ddfb0` 的提交说明写的是 `feat: Migrate the refator version of Cinux` 一句话，动机却没有落成文字的痕迹，我们把可观察的事实摆出来。而在同一笔提交里，`kernel/mini` 的 45 个文件全部删掉了，`boot/` 的 26 个新文件进了场。磁盘镜像里的 mini.bin 没有了，LBA 32 上直接放的是验收过的完整内核。当年的小内核就被装在 `0x20000` 这个地址，[引导链](../boot.md) 一页在功能概述的第二段把这件事记成了一句话：装到 `0x20000` 的小内核没有了，磁盘镜像从 LBA 32 读进来的是带验收的完整内核。

它当过的那个角色，我们再拿新旧两链对一遍：旧链是 stage2 把 mini 装进了内存，mini 再去磁盘上找它的大内核，两段加载的中间还坐着小内核这一整个内核。新链的 stage2 自己验收、自己搬运，加载器要回答的问题（装到哪、多大、入口在哪），在构建期就写进镜像头里了。旧 `ata.hpp` 注释给它的那句定位，说的是小内核拿这个驱动去加载大内核，这句话放在新链的任何文件上都套不上了。

两头都有着落的时候，中间的一层就空了出来：硬件的照应归了引导链，真正的运行归了内核本体。这是我们对着新旧两份源码读出来的事实链，作者没有另写的退场说明，这一页把事实原样记了下来。

## 源码位置

- git 提交 `cde30d1`（2026-05-25，`migrate from old Cinux`） — `kernel/mini` 的 45 个文件随旧仓库迁入
- git 提交 `49ddfb0`（2026-10-31，`feat: Migrate the refator version of Cinux`） — `kernel/mini` 全目录 45 个文件删除，旧文用 `git show 49ddfb0^:kernel/mini/文件路径` 回看
- `boot/` — 引导链本体，链条走读见 [引导链](../boot.md)
- `kernel/arch/x86_64/entry.S`、`kernel/arch/x86_64/entry.cpp` — 内核入口，GDT、TSS、IDT 与异常桩在这里装配
- `kernel/boot/boot_info.hpp`、`kernel/boot/image_header.hpp` — 交接记录与镜像头，构建与装载两侧共用的格式
- `kernel/init/init_sequence.cpp` — 内核侧初始化序列，serial、pmm、address space 三步接住引导递来的活
- 内核接棒之后的世界见 [进程与调度器](proc/process_and_scheduler.md)，任务的一生、调度与阻塞唤醒那一页按现源码重写过
