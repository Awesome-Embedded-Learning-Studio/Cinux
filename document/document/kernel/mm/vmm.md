# 虚拟内存管理

## 功能概述

这一页我们把 `kernel/mm` 里管页表的这摊事看一遍。它干的活说起来简单，就是把 x86-64 的四级页表当成一张数据结构来编辑。引导程序交过来的页表长什么样、内核自己的常驻世界怎么搭起来、设备寄存器怎么映射进内核能摸到的地址，这些活也都在它的名下。

这个子系统和旧叙事里的样子已经对不上了。今天它没有一个叫 `VMM` 的类，也没有包揽一切的 `map/unmap/translate` 接口，那些动作则被拆成了两块。公共的四层游走算法住在了 `page_walk.hpp` 里，它是一份写成了模板的算法，我们让内核世界和 host 测试世界都跑在它的上面。`vmm.cpp` 留下的则是内核自己的页表的事，接管、布局、设备窗口也都算在它的名下。任务地址空间怎么建、怎么销毁，我们交给 [地址空间](address_space.md) 一页去讲，物理页从哪来的归 [PMM](pmm.md) 一页，缺页时一页一页补上的细节住在地址空间一页的克隆一节，和 kernel/mm/cow.cpp、demand.cpp 的实现里，本页只在缺页一节里给一条简单的通路。

## 页表项的语法：`page_entry.hpp`

一切的起点是页表项本身。定义在 `kernel/arch/x86_64/page_entry.hpp` 里的类型叫 `Entry`，也就是 `BitMask<uint64_t>` 的一个别名。宽度定成了八个字节，而注释把话说得很直白，这宽度是我们按硬件事实认下来的，不管我们从哪个世界来编译都一样。我们数一下在用的几个位。第 0 位的 kPresent 说这项映射了东西，第 1 位的 kWritable 说可以写，第 2 位的 kUser 放行 ring 3，而且每一层都得带上它，中间的任何一层丢了，整条路对用户态而言就是死路。第 7 位的 kLarge 则说这项是个大页叶子，不再指向下一张表了，第 4 位的 kCacheDisable 关掉缓存，设备寄存器坚持要的正是它。第 9 位留给的是 kCow，软件自己用的，硬件是完全不过问的，写时复制就拿它标记共享的帧，写错误答的则是不再共享。第 63 位留给的是 kNoExec，它只有在 EFER.NXE（CPU 的模型专用寄存器开关，打开了硬件才认 No-Exec 位）生效了之后才有意义，NXE 没开的时候把它写进项里，招来的就是保留位缺页，所以每个映射调用都得显式把自己的标志传进来，而头文件不给任何默认值。

```cpp
// kernel/arch/x86_64/page_entry.hpp
constexpr Entry UserLeafTerms(bool writable, bool executable) {
    Entry terms = kUser;
    if (writable) {
        terms = terms | kWritable;
    }
    if (!executable) {
        terms = terms | kNoExec;
    }
    return terms;
}
```

这个函数值得我们单独看一眼，因为它是用户半区所有叶子的统一入口。按需填充、加载器装段、fork 共享的页、exec 的栈，我们数过，它们说的都是同一种话。这样的好处很具体，任何一个映射点都没法自己发明一套自己的标志，忘了带的 no-exec 位也无处藏身。三个构造函数 MakeTableEntry、MakeLargePageEntry、MakeHugePageEntry 则负责把物理地址搬进各自的字段，表指针项放的是 4 KiB 帧号，占的是第 12 到 51 位，2 MiB 大页放的是第 21 位起的字段，1 GiB 巨页放的是第 30 位起的。字段宽度和移位常量之间有 static_assert 拴着的，对不上我们就在编译期失败，没有人再去手算偏移了。

## 公共的四层游走：`page_walk.hpp`

map、unmap、translate 三个动作的算法只有一份，就住在了 `kernel/mm/page_walk.hpp` 里。我们把它写成模板，模板参数是一个叫 TableWorld 的 concept，它要求世界回答的是两个问题。`table(physical)` 要把一张表的物理地址变成可写的指针，`take_page()` 要拿出一张清过零的新表，答 0 的就算拒绝。内核世界装的是 KernelTables，表走的是直接映射（一段虚拟地址等于基址加物理地址的固定窗口），新表从 PMM 的库存里领，领到手的就已经清好零了。host 测试世界把两个口都指向一块普通的内存池，同一份游走代码两边跑的都是真页表，这正是接缝存在的理由。

地址怎么拆是这一层的另一个看点。每一层吃的是九个位，四个层级的取位范围我们挨个对一遍，它们的取法都写在 IndexRange 里，取槽用的是 SlotIndex，页内偏移用的是 LowBits，全程走的是命名枚举 WalkLevel，裸移位是一个都没有的。FindSlot 是只读的游走，而它一路往下走，碰到了大页叶子就提前停下，把槽和层一起报给了调用方去解码。EnsureLeafTable 则是带施工的游走，我们看它缺表时怎么办，缺哪层的表就从世界领一张新的挂上，路上遇到大页叶子答的就是 null，因为拆大页是调用方的策略问题，游走是不会替人做决定的。四个动作就盖在它的上面。MapPage 写的是 4 KiB 内核叶子，用 present 加 writable 的组合。MapUserPage 走的也是同一条路，但每一层、包括新造的每张表，都带上了 kUser，只有最后那个叶子用的是调用方给的整体项，表指针的写位是供应细节，叶子的位才是这一页的法律。UnmapPage 清掉的是槽位，把旧的项还给你。TranslatePage 是三种叶子通吃的，1 GiB、2 MiB、4 KiB 谁在前头我们就按谁算，物理地址加页内偏移交出去，没映射的答 0。

有一件事被有意留在了外面，那就是 TLB 的维护。游走改的只是表，不刷的是翻译缓存，因为只有调用方自己知道跑的是哪个世界、该失效的是哪个地址。内核在用的两个口都在 `kernel/arch/x86_64/registers.hpp`，InvalidatePage 是单页的 invlpg，ReloadCr3 则重装 CR3、把全部的翻译作废。我们数了数调用方，demand、cow 和 sys_munmap、sys_mprotect 的两个系统调用方各自刷自己改过的页，谁改了谁刷，游走对这些是一概不管的。

## 接管引导世界：`BringUpAddressSpace` 与种子页

内核开场的头一件内存大事是接管，入口是 vmm.hpp 里的 BringUpAddressSpace。init_sequence.cpp 的序列里它排在 pmm 之后，名字写的是 address space。它的活是把引导世界留下的几个门换成对外公布的布局，直接映射盖住全部的 RAM，帧缓冲挂进了 ioremap 别名，引导留在低半区的映射清了零。头一件事就是把引导记录 BootInfo 抄进内核自己的存储（HandoffArchive，十六字节对齐的单例）再交回去，因为低半区马上就没了，调用方手里的引用得能活过这一步，我们读代码时也要记得这一点。

这里有个鸡生蛋的难题，vmm_config.hpp 的注释交代了原委。引导门只盖住内核镜像的物理跨度，PMM 管的空闲页则落在这些映射之外，接管期去领页的时候，领回的是一张自己都读不到的表。所以接管期用的每一张表都从一个固定的低位跨度里划，也就是 kSeedTablesSpan 的 0x20000 到 0x40000，种子 pantry（代码里的 SeedPantry）就在这上面顺序切割。跨度的大小我们是按最坏的情况算的，16 GiB 管理上限（pmm_config.hpp 里的 kPmmMaxPhys）要求一张 PDPT（四级页表第二层的指针表）加十五张页目录，再加 ioremap 的一对，余量也留了。这个跨度用一次就永久归了页表所有，位置压在 PMM 低内存保留线的下面，而两边谁也不会把同一个字节给数两次。

我们跟着执行的顺序走一遍。它存档了引导记录，读的是 CR3，也就是装着页表根的控制寄存器，拿到的则是引导的 PML4，四级页表的顶层。然后我们按 1 GiB 一窗扫过物理内存。highest_usable_top 从 e820（引导程序交过来的物理内存清单）里数出可用内存的顶，封顶的线是 kPmmMaxPhys，每个碰到了可用内存的窗（window_touches_usable 查的）就从 pantry 领一张页目录，fill_window_pages 用 2 MiB 大页把整窗填满了、全带上 writable，再挂到一张 pantry 出的 PDPT 上，PDPT 最后坐进 PML4 的第 256 槽，而它就是 kDirectMapPml4。直接映射的换算是定死的，虚拟地址等于 kDirectMapBase 加物理地址，layout.hpp 里的 DirectMapVirt 一行就是全部。

直接映射挂好了之后，MapFramebufferDoor 就在存档的记录上跑一遍，把帧缓冲的别名也立起来。再往后我们要做的是两刀清理。内核镜像那个 PDPT（511 槽里的 kKernelImagePdpt 槽）以外的槽全部清零，引导世界塞在内核槽里的东西到此为止。PML4 的第 0 项也清零，低半区整个退出了历史，ReloadCr3 让新的布局生效。最后 KernelRoot 单例把此刻的 CR3 抓拍了下来，KernelPageRoot() 从此答的就是这个值。头文件对这个值的定位说得好，别的地址空间激活以后，CR3 指的是当下活着的那个世界，这个值则是回家的路。前置条件注释也写明了，Pmm::init 要排在它的前面，而中断也得是关着跑的，接管是一次性的仪式，而表也没有谁来跟它抢。

## 设备窗口：ioremap 别名

设备内存走的是 PML4 第 259 槽 kIoremapPml4。这个槽里的面孔有两张，我们分开认。MapFramebufferDoor 是开场的那个，PML4 的 ioremap 槽直接指一张 PDPT，PDPT 下面的每个 1 GiB 窗各一张页目录，目录里用 2 MiB 大页把帧缓冲盖住了。它的种子表从 pantry 来、不带 PMM、也不拆低半区，注释特意留了话，只要屏幕不要整个接管的测试内核，单跑这个函数也是可以的。

MapDeviceWindow 则是后来装的正式口，我们把它的签名读一遍，收的是物理基址加字节数。它从 KernelPageRoot() 拿的是内核根，缺的 PDPT 和页目录从 KernelTables 领，这时候的 PMM 已经活了。每个 2 MiB 片都带上了 kCacheDisable，寄存器读到的才是设备而不是一份缓存副本。窗口按大页的边界向外取整，跨界的设备区域也能盖全。它编辑的是内核自己的根，注释点破了这手的用意，任务空间的内核半区是从内核自己的根镜像过去的，设备窗口挂了一次，后建的每个地址空间自动继承，谁也不用再单独挂一遍了。一段区间没全部落进活着的页表时它答的就是 false，怎么处置由调用方自己定。

## 缺页之后：从 fault 到 FillPage

旧页写缺页处理的时候只有一行读 cr2 加按需分配，今天的链子值得我们把骨架摆出来，因为它正好反过来衬出 vmm.cpp 的分工。缺页中断的本体在 `kernel/arch/x86_64/exception.cpp`，而它办的头一件事就是把地址和错误码交给一个注册进来的 resolver，resolver 答了真，我们的缺页就到此结束。内核装的这个 resolver 住在 `kernel/proc/fault_resolve.cpp`，它的座位是 InstallPageFaultResolver 安的。它查当前任务有没有地址空间和 VMA（记录任务里一段地址归属）的表项，查这一回的访问在权限和栈界限上站不站得住，然后按顺序问的是两个部门，写时复制的 TryResolvePageFault 答不了再问 demand 的 FillPage，两个都答不了、错误又来自用户态，任务就领一个 SIGSEGV 退了场。resolver 答不了的时候，异常也没有立刻走上打印的路，内核态的缺页还问异常修复表兜不兜得住，兜住了就改返回地址走人。这些环节都兜不住的时候，才轮到 diagnoser 和 report_page_fault（沿着 CR3 把 pml4e 到 pte 的四层逐层打出来，哪层断了它就停在哪），打完机器就停在 PanicDump 上了。cow.cpp 自己也装过一次自己的 resolver，那是它测试场景的座位。

FillPage 的本体在 `kernel/mm/demand.cpp`，我们只看它和页表的接触面。错误码带保护错或保留位、地址越过 kUserTop 的直接拒。VMA 表里找不到区间的也拒。共享区间把共享帧直接挂进的是用户叶子，给帧记了一次持有。设备区间领的不是 RAM 帧，问设备的 frame_at 要硬件帧。普通区间从 PMM 领一页、经直接映射清了零，文件背书的区间再把文件的一页读进帧里，读短了的按 bss 处理，领不到帧或者 map_user 写不进去的就把帧退回去。每条成功路径的收尾都是同一个动作，map_user 挂叶子加 InvalidatePage 刷掉旧的翻译。区间、按需填充、写时复制各自的细节住在地址空间一页的克隆一节，和 kernel/mm/cow.cpp、demand.cpp 的实现里，这里只立的是一条通路。

## 设计取舍

我们把几个为什么收在末尾。算法我们只写一份、世界各自供货，换来的是 host 测试不碰机器就能跑真页表，代价是每个调用点多传一个 world 的参数，我们认为这样的换法是划算的，test/unit 和 test/kernel 下的 test_vmm.cpp 就是证据。TLB 则刻意留在游走的外面，理由前面说过了，只有调用方自己知道改的是哪个世界的哪个地址，集中刷全量当然省心，但在没有 SMP 广播的今天，单页 invlpg 的纪律更便宜也更准。表指针的写位和叶子的权限位分开，是同一类取舍的另一面，表指针管的事只是把路修通，而页面的权限落在了叶子上，MapUserPage 的注释拿一句话就把这事定了性。大页在整个系统里我们都用得很放手，直接映射用的全是大页、ioremap 也是，表页开销就因此缩到了十几张，而 4 KiB 粒度的动作遇到大页叶子时游走就直接罢工了（EnsureLeafTable 答 null），拆页的决策就推给了将来需要的那个调用方，眼下的调用方还没有谁需要它。种子页的固定跨度看着像魔法数字，其实也是一种换法，接管期没有分配器是可以依赖的，那就让表页的来源固定在 0x20000 到 0x40000 这一段，有上界的，用完永久出借的。

## 源码位置

- `kernel/mm/vmm.hpp`、`kernel/mm/vmm.cpp` — BringUpAddressSpace、MapFramebufferDoor、MapDeviceWindow、KernelTables、KernelPageRoot、种子 pantry
- `kernel/mm/page_walk.hpp` — TableWorld 接缝、FindSlot、EnsureLeafTable、MapPage、MapUserPage、UnmapPage、TranslatePage
- `kernel/mm/vmm_config.hpp` — kSeedTablesSpan 与接管期表页供应的取舍说明
- `kernel/mm/layout.hpp` — 对外公布的地址空间布局与各基址的 static_assert
- `kernel/arch/x86_64/page_entry.hpp` — Entry 位语法、UserLeafTerms、三级叶子构造
- `kernel/arch/x86_64/registers.hpp` — ReloadCr3、InvalidatePage（invlpg）
- `kernel/proc/fault_resolve.cpp`、`kernel/mm/demand.cpp` — 缺页 resolver 与按需填充
- `kernel/init/init_sequence.cpp` — 接管在启动序列里的位置
- `test/unit/test_vmm.cpp`、`test/kernel/test_vmm.cpp` — 两个世界下的游走测试
