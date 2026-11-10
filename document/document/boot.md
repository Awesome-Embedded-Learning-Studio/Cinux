# 引导链

## 功能概述

这一页我们把 Cinux 从上电到内核开口说话的整条引导链走一遍。链条一共走了三程，BIOS 把磁盘的第一个扇区搬到了 0x7C00 的座位上，那里住的就是 MBR（主引导记录），MBR 把 stage2 搬到了紧随其后的 0x7E00，stage2 在实模式里把 BIOS 的家底问清楚、把内核镜像验明正身搬进内存，而后借道保护模式打开长模式，在世界开关（源码注释里的 world switch，stage2 把机器交给内核的那一下）里把交接记录递到了内核的手里。交接用的两个信箱（两处定好了地址的内存字，展开的细节放在交接记录一节）、交接页表的搭法、内核入口接活的样子，本页都按当前的源码写。模式探询的细节与帧缓冲的内核侧接手归 [视频子系统](kernel/drivers/video.md)，E820 的清单怎么变成空闲位图归 [物理内存管理（PMM）](kernel/mm/pmm.md)，地址空间的接管归 [虚拟内存管理](kernel/mm/vmm.md)，至于链条本身的来龙去脉，我们就在这一页里把它讲清楚。

旧页的几处事实已经过期，我们按现在的代码纠正。VESA 不再写死 0x144 这样的固定模式号，模式号是探询问出来的。装到 0x20000 的小内核没有了，磁盘镜像从 LBA 32 读进来的是带验收的完整内核。调试标记 P、L、J 与 debugcon 端口也退场了，引导世界的播报如今全走 COM1 串口。

## 座位与来路：MBR、stage2 与磁盘布局

三个座位都是定下来的数，boot/layout.hpp 的文件头注释说这些常数是约定而不是偏好。BIOS 永远把 MBR 放在 0x7C00 的位置，stage2 永远跟在它身后一个扇区的地方，座位写在 layout.hpp 的 kStage2Spot 里，它带的地址是段 0x0000 加偏移 0x7E00，一共占了 28 个扇区。我们把 MBR 的活计在 boot/mbr.cpp 里读一遍：入口的内联汇编把段寄存器清了零、把栈顶定到了 0x7C00，再把 BIOS 递来的启动盘号 %dl 存进 g_boot_drive，源码注释交代了缘由，链接器不知道这个符号的存在，最后是汇编亲手把它喂了进来。MbrMain 办的事只有一件：把 DAP 按 kStage2Spot 的座位填好，DAP 是磁盘地址包，INT 0x13 扩展读用的参数包，AX=0x4200 的请求从 LBA 1 把 28 个扇区读进 0000:7E00，读失败就借 BIOS 的电传打字打出一个 E 加两位错误码然后 hlt 停机，读成了就一个远跳跳进 stage2。mbr.ld 里的断言看住了尺寸，正文的部分不许越过 510 字节的线，剩下的位置留给了 0xAA55 的启动签名。

磁盘镜像的拼装归 boot/CMakeLists.txt 的 add_cinux_image 管：dd 把 mbr.bin 写到了 0 号扇区，stage2.bin 写在了 1 号扇区起的位置，内核的 kernel.bin 写到 LBA 32，跟 layout.hpp 里的 kKernelImageLba=32 对上了。构建还多看了一步，stage2_size_check 用正则从 layout.hpp 里把 .sectors 的数值抠出来，stage2.bin 一旦超过 28 个扇区的容量，构建的这一关就直接过不去，报错文本写明该去抬高 kStage2Spot 的扇区数，MBR 的 DAP 会跟着走。内核的 kernel.bin 是链接完成的 ELF 用 objcopy 转成的平铺二进制，镜像头的生成靠的是 kernel.ld 与 image_header_data.cpp 的合作，我们到交接记录一节再看它。

## 实模式一程：串口、A20、E820 与模式探询

stage2 的入口内联汇编跟 MBR 是一个路数：它关了中断、清了段寄存器、把栈顶定在 0x7000，然后就调进了 Stage2Main。栈顶那行的后面跟着一句源码注释，Can we trust the new envs? better not，作者的答案是宁可不信任新环境。Stage2Main 的次序我们顺着读：InitConsole 把 COM1 配到了 115200 8N1，开了 FIFO，关了中断，boot/early/console.hpp 的文件头注释说明了这样安排的道理，引导镜像把 16、32、64 位的代码混在了一份里，所以控制台做成了头文件内联的形态，每个编译的世界各得一份，内核树里驱动版的控制台与它无关。此后引导世界的每句话都从串口出去，头一行报的就是 `[stage2] stack ok`。

A20 的本体是第 21 根地址线上的门电路，不开的话实模式一摸到 1 MiB 以上的地址就会回卷到低处去，打开它的动作走的是 INT 15h AX=2401 这么一条道，bios.hpp 的注释把这个取舍写得很直白，模拟器固件一定会提供这样的服务，回退的链条在作者眼里属于过度设计，我们也就不备第二手。返回值的判法里藏了个细节，bios.cpp 里的 setnc 按进位标志置了答案，注释补了一句，A20 与 E820 的 carry 语义相反，成功的标志是 CF=0。

E820 的收集我们交给 CollectMemoryMap 循环来做，每一次 AX=0xE820 的问询要回一条，BX 里的续传令牌归零才算问完，条目的上限是 kE820MaxEntries=32。每条记录要求 BIOS 至少回报 20 字节的数据，bios.cpp 用结构体的大小减掉 acpi_attr 字段，算出了 20 字节的下限，SMAP 签名与 AX 的回执都要对上。收来的清单铺在串口上，dump_memory_map 把每条的基址与长度按十六列宽零填充打出来，源码注释说这个定宽是照着当年在 GDB 里读地址的排法定下的。分类归 boot/e820/e820.hpp 的 ClassifyEntry，认得的类型照实归档，认不得的一律按保留算，注释给的立场是把可用误记成保留只浪费几个页，反过来就要踩到 BIOS 留下的洞。

VBE 的探询我们把链子过一遍，帧缓冲侧的细节归 [视频子系统](kernel/drivers/video.md)。把取控制信息块的问句发出去之前，得把 VBE2 的签名填进缓冲，bios.hpp 的注释说 BIOS 认了签名才肯给 2.0 以上版本的数据。模式列表是一条段加偏移的远指针，stage2.cpp 的注释照实交代了折法，段左移四位加偏移折成 DS=0 的线性地址。probe_video_mode 沿着列表逐个地调 QueryModeInfo，单个模式问砸了就跳过去继续，问到 128 个的上限（kMaxModesToProbe）就停。请求写在文件顶部的常量里，分辨率要的是 1024×768，位深要的是 32，24 位做了备选。中选的判据在 MatchesRequest，它把五样都看了：线性帧的属性位要亮，宽与高要对得上我们要的数，要求的位深也不能差，帧地址为零的照样不要。旧页写的 0x144 在当前代码里没有对应物，模式号是探询问出来的，设置的时候，SetVideoMode 把线性帧缓冲的请求位 0x4000 按了进去。

调试这件事我们也照实记一笔。旧页的调试标记一节整段过期，P、L、J 三个单字与 0xE9 的 debugcon 端口在当前代码里都没有对应物，每个世界开场在 COM1 上各报一行自己的名号。layout.hpp 的文件头注释里还留着一句 debug console 应答在 0xE9 端口，跟 console.hpp 的实际接线对不上，读的时候以代码为准。

## 内核镜像的验收与搬运

内核镜像住在磁盘的 LBA 32 起，我们把它的来路认一遍：镜像的最前头躺着它的 ImageHeader。stage2 不碰 ELF 的解析：链接脚本 kernel.ld 把装载地址与文件大小、内存大小、入口地址都算成了符号，image_header_data.cpp 把符号折成一张 40 字节的头，安在装载地址 0x200000 上的 .image_header 段里，随后跟着镜像一起 objcopy 成了平铺的二进制。两侧共用的是同一份 kernel/boot/image_header.hpp，文件头的注释说这是构建与装载器之间的共同格式，验收的尺子就是它，两边的定义都住在一份头文件里，所以格式也就漂不掉。

我们按次序把验收走一遍。验收之前 stage2 把 E820 的可用区收拢成一张小表，只收类型为 1 的条目，最多收满了 16 条（kMaxUsableRegions），再把引导自己占了的地皮列成四条，头一条的地界从 0 一直铺到了 0x20000，ferry（借保护模式往返搬内核的通道）的窗口，与读回来的头暂放的 0x4000 草稿区，都圈进了头一条的地界，一条是 stage2 的座位，一条是 0x1000 到 0x4000 的页表区，最后一条从 0x6000 铺到了保护模式的栈顶 0x90000，实模式栈、MBR 与 stage2 自己的镜像都在带里。验收的清单是这么走的：魔数要对上 0x00114514，版本号要认的是 1，文件的大小不能是零，内存的大小不能小于文件的大小，地址的算术不能溢出，镜像的收尾不能越过交接门的上限 0x40000000（kHandoffDoorTop，交接门是后文的页表给交接阶段开的映射窗口），入口的位置要落在镜像之内，镜像的全身要落进某一条可用的区里，还不能跟引导的地皮重叠，九条全过了才是 kOk。任何一条拦了下来，都会把 NameOf 给出的理由打上串口然后 Halt，文件头的注释说尺寸与算术摆在前头，假的头在前几条就被拦住，内存现实就不用扫了。

搬运走的是 ferry，我们把往返的路读一遍。实模式的手就算 A20 开了，也只够得着头 1 MiB 稍多一点的地方，内核却要落在 2 MiB 的地方，所以每个窗口都得借保护模式过一趟。ReadHeader 的头一句就是 lgdt kGdtr，lgdt 是装表的指令，把 GDT（全局描述符表）的基地址喂给了处理器，loader.hpp 的注释说这样 ferry 的往返就已经在切换用的描述符底下跑了。读头的动作本身只搬一个扇区到 ferry 的窗口，再由 RunFerry 抄到 0x4000 的草稿区。ferry 的往返在 boot/load/ferry.cpp 里，PmVisit 把 CR0 的 PE 位置起来，远跳进 0x08 选择子的 FerryEntry32，32 位世界里 CopyFlat 把字节抄到目的地，回头要穿 0x28 的 16 位代码描述符出去，gdt.hpp 的注释交代了它的用处，远跳把 CPU 对操作数尺寸的认知也复位了，这才回得去实模式继续用 BIOS 的服务。CR0 的 PE 位清掉了、段寄存器也归了零，一次搬运就回来了。窗口的容量一次最多 127 个扇区（kFerryWindowSectors），LoadKernel 循环着把全部的文件搬完，头一个窗口搬完还要回头把落在目的地的头 8 个字节抄回来对魔数，loader.hpp 的注释说这是交付后的核对，对不上回的是 kMagicMismatch。搬运成功了之后，LoadKernel 把 g_kernel_entry 与 g_kernel_end_paddr 两个全局填上了，后一个在页表的一节里要用到。

GDT 的六项在一次 lgdt 里就全齐了。gdt.hpp 的注释把这个设计说得很清楚，我们也照着读：表天生带着长模式的一对描述符和 16 位代码段，保护模式切换的那一次 lgdt 就把引导链要装的所有选择子放行了。平坦段的基址是零、界限跨满了 4 GiB，长模式代码描述符的 L=1 与 D=0 是架构规定成对的搭配，字节布局的形状有 test_gdt 在 host 测试那边看着。

## 交接记录与两个信箱

交接记录的本体叫 BootInfo，定义的文件是 kernel/boot/boot_info.hpp，我们把它的字段认一遍：

```cpp
// kernel/boot/boot_info.hpp
struct [[gnu::packed]] BootInfo {
    uint32_t        magic;
    uint32_t        version;
    uint32_t        struct_size;
    uint32_t        e820_count;
    E820Entry       e820[kBootInfoE820Max];
    FramebufferInfo framebuffer;
    uint64_t        kernel_paddr;
    uint64_t        kernel_file_size;
    uint64_t        kernel_mem_size;
    uint64_t        kernel_entry;
};
```

magic 与版本号来自构建期的常量，我们把它们跟镜像头对了个表，kBootInfoMagic 的值跟镜像头的魔数是同一个数，写出来的数就是 0x00114514。e820 的数组按 kBootInfoE820Max=128 留了座，实际的条数以 e820_count 为准，BIOS 那边收满了 32 条就停，每条是基址、长度、类型三个字段排成的 20 字节记录。FramebufferInfo 装的是帧的几何与三路颜色掩码，packed 的讲究与尺寸断言，[视频子系统](kernel/drivers/video.md)那一页已经写过了，我们不重复。后半段的四个内核字段全部出自镜像头，它们回答的是装到哪、文件多大、连 BSS 在内占了多大、入口在哪。fill_boot_info 把这些字段逐个地填进了 g_boot_info，记录的本体住在 stage2 自己的数据里。

交接靠的是两个信箱，两个地址定在了 layout.hpp 里，kHandoffMailboxEntry 的地址是 0x4F00，kHandoffMailboxInfo 的地址是 0x4F08。Stage2Main 的收尾三行把交接办完，我们把这三行原样搬下来：

```cpp
// boot/stage2.cpp
    fill_boot_info(kHeader, memory_map, kFramebuffer, choice.info);
    StoreWord(kHandoffMailboxEntry, static_cast<unsigned long>(kHeader.entry));
    StoreWord(kHandoffMailboxInfo, reinterpret_cast<unsigned long>(g_kernel_boot_info));
```

StoreWord 是 volatile 的写，我们把 layout.hpp 注释里的理由读全，写它的是一个世界，读它的是另一个世界，读的动作必须真的发生，写的动作必须赶在任何消费它的世界开关之前落进内存。旧页说 BootInfo 是填在 0x7000 的，当前的代码里没有这件事，信箱里递的是 g_boot_info 本体的地址。E820 的存放处也变了，MemoryMap 住在 stage2 的 .bss 里，e820.hpp 的注释特意说别假设它开局是全零，旧页写的 0x5000 同样没有对应物了。

## 页表搭建与长模式开关

我们把保护模式的三步走完：EnterProtectedMode 做的是 lgdt kGdtr，把 CR0 的 PE 位按了上去，远跳进 0x08 选择子里的 PmEntry。PmEntry 把数据的选择子逐个装进了段寄存器，栈顶挪到了 kPmStackTop 也就是 0x90000，串口上打出的是 `[pm] 32-bit world alive`，随后的 BuildHandoffDoors 把交接页表搭好，EnterLongMode 接着把长模式也打开了。

页表的座位定在 layout.hpp 的 kPageTables 区里，我们把三张表的来历读一遍：从 0x1000 到 0x4000 一共 12 KiB 的地界，正好三张 4 KiB 的表。boot/lm/lm.hpp 把三张表的地址从这块区里推导了出来，PML4 坐在区的头上，PDPT 与 PD 各自往后挪了一页，文件头的注释说这么排的用意，填表的与切模式的读同一份定下来的事实，三个数字也就不用各养各的了。表的基址对齐在 4 KiB 的整数上，PD 再添一页也越不出页表区的上沿，这两件事就交给 static_assert 在编译期替我们看着了。

BuildHandoffDoors 的全部活计住在一个文件里，文件的名字是 boot/lm/page_tables.cpp，我们把代码读一遍：

```cpp
// boot/lm/page_tables.cpp
extern "C" void BuildHandoffDoors() {
    auto* const kPml4 = table_at(kPml4Phys);
    auto* const kPdpt = table_at(kPdptPhys);
    auto* const kPd   = table_at(kPdPhys);

    unsigned long const kTableBytes = sizeof(Entry) * kEntriesPerPage;
    cinux::base::SetBytes(kPml4, 0, kTableBytes);
    cinux::base::SetBytes(kPdpt, 0, kTableBytes);
    cinux::base::SetBytes(kPd, 0, kTableBytes);

    unsigned long const kCovered   = g_kernel_end_paddr + 0x1FFFFFUL;
    auto const          kDoorCount = static_cast<unsigned int>(kCovered >> 21);
    for (unsigned int i = 0; i < kDoorCount; ++i) {
        kPd[i] = MakeLargePageEntry(i * cinux::arch::page::kLargePageSize, kWritable);
    }

    kPdpt[0]   = MakeTableEntry(kPdPhys, kWritable);
    kPdpt[510] = MakeTableEntry(kPdPhys, kWritable);
    kPml4[0]   = MakeTableEntry(kPdptPhys, kWritable);
    kPml4[511] = MakeTableEntry(kPdptPhys, kWritable);

    auto const&    frame = cinux::base::PtrAt<const cinux::boot::BootInfo>(
                           cinux::boot::LoadWord(cinux::boot::kHandoffMailboxInfo))
                           ->framebuffer;
    uint64_t const kFrameBytes =
        static_cast<uint64_t>(frame.pitch) * static_cast<uint64_t>(frame.height);
    auto const kDoors = PlanDeviceDoors(frame.physical, kFrameBytes);
    for (uint64_t door = 0; door < kDoors.count; ++door) {
        uint64_t const kIndex = kDoors.first_index + door;
        kPdpt[kIndex]         = MakeHugePageEntry(kIndex * kHugePageSize, kWritable);
    }
}
```

我们把这段代码一句句读完。开头的三行 SetBytes 把三张表清了零，接着的映射给内核镜像搭身份门，kCovered 是内核收尾的物理地址向上对齐到 2 MiB 的结果，PD 里逐条地写上 2 MiB 的大页，内核镜像装在了哪里，身份的映射就照到哪里。然后是同一个 PD 的两种挂法，PDPT 的 0 号与 510 号、PML4 的 0 号与 511 号都指向它：0 号一路是低处的身份映射，511 号一路把同样的映射搬到高处 kHighHalfBase 起的窗口，kernel.ld 把内核的链接地址定在了 0xFFFFFFFF80200000，数值上它正好等于高处的基址加装载地址 0x200000，一条映射就这样服务了两个地址。帧缓冲的 1 GiB 大页窗口也在这个函数里搭，帧的几何是从交接信箱里现读的，写出来的条目落进了 PDPT，规划与断言的细节 [视频子系统](kernel/drivers/video.md)已经写过了，我们不重复。旧页写的临时页表映射 0 到 8 MiB 不再成立，盖多大由内核镜像自己的收尾决定。

EnterLongMode 的四拍我们按次序念：CR3 装进 PML4 的物理地址，CR4 按上了 PAE（物理地址扩展，长模式的前置）的位，rdmsr 与 wrmsr 这对读写 MSR（模型专用寄存器）的指令给 EFER（管长模式的那个 MSR）置上了 LME（长模式使能）的位，CR0 按上了 PG 位，把分页也打开了，长模式就随分页一同活了过来，最后的远跳落进了 64 位的代码选择子 0x18，目标定在了 0xB000。EFER 的两个常数在 lm.hpp 里紧挨着住，文件头的注释交代了这段来历，当年的错误把 SVME（相邻的虚拟化使能位）的 0x1000 写在了 LME 的 0x100 该在的位置，后来作者把两个常数放到了一起当对照，并排的两行里一个零的差别一眼就能看出来，static_assert 也加了一条保证它们不相等。跳的目标 0xB000 是 kLmEntryVma，layout.hpp 的注释说它一数两用，既是远跳的目标，也是 64 位小块自己的链接基址。

64 位的小块是怎么进的镜像，我们到构建脚本里看一眼。做法的记载在 boot/CMakeLists.txt 里，构建用的是 `add_cinux_boot_binary(lm_blob 64 lm/lm.ld)` 这一行，外加的是 -mcmodel=small 的编译选项，objcopy 把它转成了 elf32 的对象，段名也改成了 .lm_blob，再跟 16 位的 stage2 链接在一起，stage2.ld 把 .lm_blob 安在了 0xB000，ASSERT 则写在 lm.ld 自己的脚本里，.text 的地址要是不落在 0xB000 上就报错。远跳跳的固定偏移与小块链接的基址共用一个数，咬合的默契就落在这同一个数上，错位的意外也就轮不上了。

## 世界开关的落点：内核接手

远跳落地的地方是 boot/lm/lm.cpp 里的 LmEntry，我们把它进门后的几拍读完：它进门把 0x20 的 64 位数据描述符装进了各段寄存器，栈顶也再次对到了 0x90000，串口上打出的是 `[lm] 64-bit world alive`，然后就到了读信箱的一步：

```cpp
// boot/lm/lm.cpp
    unsigned long const kEntryTarget =
        cinux::boot::kHighHalfBase + cinux::boot::LoadWord(cinux::boot::kHandoffMailboxEntry);
    unsigned long const kInfoAddress =
        cinux::boot::kHighHalfBase + cinux::boot::LoadWord(cinux::boot::kHandoffMailboxInfo);
    asm volatile(
        "movq %[entry], %%rax\n"
        "movq %[info], %%rdi\n"
        "jmpq *%%rax\n"
        :
        : [entry] "r"(kEntryTarget), [info] "r"(kInfoAddress)
        : "rax", "rdi", "memory");
```

信箱里的入口地址还是物理的，我们把换算的算法念一遍：高处基址一加，就成了内核自己的虚拟地址，跟内核链接的基址走的是同一条算法。rdi 里装的是交接记录的地址，x86-64 的调用约定里第一个参数走 rdi，对内核来说这就是函数的第一个实参。jmpq 跳过去就不回头了，后面的 __builtin_unreachable 把退路也封掉了。

内核侧的头几拍，我们挪到 kernel/arch/x86_64/entry.S 里看。KernelEntry 把 CR0 的 x87 仿真位清掉、协处理器监视位按上，CR4 打开 SSE 的两个使能位，注释特意记了一句，rdi 带着的是 stage2 递来的 BootInfo 地址。随后的栈切到了 g_kernel_stack_top，call 进 entry.cpp 的 KernelStart：BSS 也清了零，栈金丝雀从 rdtsc 的周期数播种，注释强调的次序是清零之后、任何受保护代码之前。CR0 的写保护位也打开了，内核自己的 GS 基址、GDT、TSS（任务状态段）、IDT（中断描述符表）与异常桩（代码里的 exception stub）逐个上岗，然后把 rdi 折成了引用传给 kernel::Main。

Main 接手后的头一件事是把内置的 kallsyms（内核符号表）装好，RunInitSequence 的序列跟在后面，我们跟着头三步走。kernel/init/init_sequence.cpp 的序列里头三步跟引导链直接接壤：serial 的一步点亮了串口，pmm 从交接记录的 E820 里搭起空闲位图，消费的细节归 [物理内存管理（PMM）](kernel/mm/pmm.md)。address space 的一步调的是 BringUpAddressSpace，把引导的页表换成了内核对外公布的布局，交接记录也由它抄进了内核自己的 HandoffArchive，低半区的身份映射随后清零并刷新 CR3，引导留下的映射到此清场，世界就完全归了内核。接管的完整走读归 [虚拟内存管理](kernel/mm/vmm.md)，内核半的复制是怎么做的、KernelPageRoot 抓拍的是哪一刻，[地址空间](kernel/mm/address_space.md)的一页接着讲。Main 自己打印的 `[kern] bootinfo: N e820 entries, fb W*H*B` 一行，就是交接记录活着的凭证。

## 源码位置

- `boot/mbr.cpp`、`boot/mbr.ld` — MBR 本体、DAP 扩展读与 510 字节断言
- `boot/layout.hpp` — 座位常数、交接信箱地址、LoadWord/StoreWord、kHighHalfBase
- `boot/stage2.cpp`、`boot/stage2.ld` — Stage2Main 的实模式次序、模式探询、ValidateImage 的调用（load_kernel_image）与信箱收尾
- `boot/bios.hpp`、`boot/bios.cpp` — A20、E820、VBE 三类 BIOS 问句的实现
- `boot/e820/e820.hpp` — E820 条目形状、分类与 32 条上限
- `boot/vesa/vesa.hpp` — 模式信息块与 MatchesRequest
- `boot/gdt/gdt.hpp`、`boot/gdt/gdt.cpp` — 六项引导 GDT 与 kGdtr
- `boot/pm/pm_switch.cpp`、`boot/pm/pm.cpp` — 保护模式开关与 32 位一程
- `boot/lm/page_tables.cpp` — BuildHandoffDoors 的身份门与设备门
- `boot/lm/lm.hpp`、`boot/lm/lm_switch.cpp` — EFER/CR4/CR0 常数与长模式四拍
- `boot/lm/lm.cpp`、`boot/lm/lm.ld` — LmEntry 世界开关与 0xB000 链接断言
- `boot/load/loader.hpp`、`boot/load/loader.cpp` — 头部读取与搬运的窗口循环
- `boot/load/ferry.cpp`、`boot/load/ferry32.cpp` — 借道保护模式的搬运往返
- `boot/early/console.hpp`、`boot/early/print.hpp` — 引导世界的 COM1 控制台与格式化打印
- `kernel/boot/image_header.hpp` — 镜像头、验收清单与 LoadStatus
- `kernel/boot/boot_info.hpp` — BootInfo 交接记录
- `kernel/arch/x86_64/kernel.ld`、`kernel/arch/x86_64/image_header_data.cpp` — 内核链接基址与镜像头的生成
- `kernel/arch/x86_64/entry.S`、`kernel/arch/x86_64/entry.cpp` — 内核入口与 KernelStart
- `kernel/kernel.cpp`、`kernel/init/init_sequence.cpp` — Main 的开场与初始化序列
- `boot/CMakeLists.txt`、`cmake/boot/stage2_size_check.cmake` — 磁盘拼装与 stage2 尺寸检查
- `test/unit/test_layout.cpp`、`test/unit/test_gdt.cpp`、`test/unit/test_e820.cpp`、`test/unit/test_vesa.cpp`、`test/unit/test_image.cpp`、`test/unit/test_page.cpp` — host 侧的形状与断言
