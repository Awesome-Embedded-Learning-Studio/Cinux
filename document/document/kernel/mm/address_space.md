# 地址空间

## 功能概述

这一页我们把 `kernel/mm/address_space` 里的地址空间子系统完整地走一遍。AddressSpace 是每个进程一份的翻译世界：它拥有一棵自己的顶层页表，内核的那一半（PML4 的 256 到 511 号表项）从内核自己的根原样复制过来，用户的那一半从空白开始，只靠这个空间自己的映射慢慢长大。头文件的注释把一生列得很完整，从头一回的建造，到内核半的镜像，再到后面的映射、翻译与切换，最后的退役也在它的手里，这一生的活它全包了。

物理页的来路与归还，是物理内存管理那一页讲过的事，我们就不重复了。内核自己的页表在接管阶段怎么铺开，那件事我们留给 vmm 那一页去管，那一页已经按新实现重写了，开头自己声明了它和旧叙事的差别。控制块本体落在了内核的堆上，堆那一页管的就是分配这件事。写时复制的引擎是 fork 的地基，我们后面在克隆与退场两段里把它当依赖来用，引擎自己的细节等它那一页重写。

## 数据结构：AddressSpace

类的定义在 `kernel/mm/address_space.hpp`，我们把瘦身后的样子放在这里看：

```cpp
// kernel/mm/address_space.hpp
class AddressSpace : public cinux::base::RefCount<> {
public:
    [[nodiscard]] bool init();
    [[nodiscard]] unsigned long root() const;
    [[nodiscard]] bool map(unsigned long virtual_address, unsigned long physical) const;
    [[nodiscard]] bool map_user(unsigned long virtual_address, unsigned long physical,
                                cinux::arch::page::Entry leaf_bits =
                                    cinux::arch::page::kWritable | cinux::arch::page::kUser) const;
    [[nodiscard]] unsigned long translate(unsigned long virtual_address) const;
    void activate() const;
    void teardown();
    ~AddressSpace() { teardown(); }
private:
    unsigned long root_ = 0;
};
```

它的私有字段只剩一个 `root_`，里面存的是这棵页表根的物理地址，没 init 过的时候它是 0，后面的每个方法都拿它当进门要过的检查。类继承了 RefCount 的引用计数形态，拷贝与移动的禁用由它承担，这件事的出处是 RefCount 那边的注释，它的文件是 `base/include/cinux/ref_count.hpp`，注释把这里的事说得很直白，所有权属于造它的那个对象，谁也别想把所有权复制了带走。RefCount 是 base 层的侵入式计数，我们每次 retain 与 release，走的都是 acquire release 的原子序，归零之后还有人想来 retain 的话，Check 当场就把这个调用拦了下来。

我们读头文件的注释就知道，单例的帽子它戴不上，注释把话说得明明白白的，每个世界都有自己的一份，将来的主人就是进程。实例是我们用 new 造在堆上的，随后它们就归 `base::Shared` 持有了。持着它的人是谁，我们看 Task 的字段就明白了：`user_root` 存根的物理地址给切换用，`space` 这个 Shared 才是真正握着世界的地方。头文件的注释把它的命运交代了，CLONE_VM 的克隆给同一个世界添一个持有者而不复制，最后一个走出来的把用户半边收掉。

## 出生：init 与内核半镜像

init 的实现很短，我们把整段读下来：

```cpp
// kernel/mm/address_space.cpp
bool AddressSpace::init() {
    base::PhysAddr const kFrame = Pmm::self().allocate_page();
    if (kFrame == base::PhysAddr{}) {
        return false;
    }
    auto* const kFresh = cinux::base::PtrAt<Entry>(DirectMapVirt(kFrame.raw));
    cinux::base::SetBytes(kFresh, 0, cinux::arch::page::kSize);
    const Entry* const kKernel = cinux::base::PtrAt<Entry>(DirectMapVirt(KernelPageRoot()));
    cinux::base::CopyBytes(kFresh + kKernelHalfSlots, kKernel + kKernelHalfSlots,
                           kKernelHalfSlots * kSlotBytes);
    root_ = kFrame.raw;
    return true;
}
```

它的执行顺序是四拍，我们一拍一拍跟着走。我们头一拍就向 PMM 要一页做根表，要不到的话它就答 false。接着我们借直接映射的通道把这一页清成空白，直接映射就是内核给自己留的一块照着物理地址直读的窗口，随后我们到 `KernelPageRoot()` 那棵根上取货，把 256 个内核的表项、合计两 KiB，原样搬进新根的上半。末了我们把物理地址记进 `root_`，出生的手续就算办完了。头文件的注释特意强调了一句，机器安安稳稳地待在原地，CR3（装着当前页表根物理地址的那个寄存器）指着的还是原来的世界，全部的建造工作都借着直接映射完成，所以我们在哪个空间里调 init 都一样安全。

内核半为什么要复制而不是共享，注释给出的理由是镜像让内核代码在任何世界里都活着：不管 CR3 指到哪个用户的世界，高半的映射都还在，内核的代码、直接映射与 per-CPU 块一个都不会丢。这个复制每次造空间只花两 KiB 的一次性搬运，换来的是切换的时候不需要任何修补动作，我们认为这笔交换是划算的。`KernelPageRoot()` 记的是 BringUpAddressSpace 在接管那一刻抓拍下来的根，vmm 头文件的注释把它的身份说成回家的路，别的世界活跃起来以后 CR3 不再指它，它却一直是回内核的路。

## 映射与翻译：借道共享走表

map、map_user 和 translate 三个方法是懒得自己走表的，它们把活转给了 `kernel/mm/page_walk.hpp` 里的模板（这个 concept 叫 TableWorld），配的世界是 `KernelTables`。走表的逻辑，整个仓库里只写了一份，page_walk 的头文件注释告诉我们，每个世界只要答得上它的两个问题，同一套走表也就跑起来了。一个是给定物理地址怎么够到可写的表页，另一个是新的表页从哪里来。内核世界用直接映射够表、从 PMM 的库存里领清零页，host 测试世界把两个口都指到一个普通的内存池上，于是同一份走表的代码在真表与测试池上都跑得动，`test/kernel/test_cow.cpp` 里的用例就是这么驱动的。

map 走的是内核侧的 MapPage，沿途的表项一律写成 present 加 writable。map_user 多了一件我们必须交代的事：走表经过的每一级都要带上 kUser 位，连沿途新建的表也一样，page_walk 的注释把原因写得很清楚，任何一级丢了通行位，它下面的整条路都走不下去，ring 3（用户态）就到不了那一页。叶子表项的权限则只由调用者给的 leaf_bits 决定，注释里的原话是表指针的写位只是供给的细节，叶子上的位才是这一页的法律。默认的参数保留了可写加用户的历史形状，页错误路径与加载器传进来的则是更窄的条件。

translate 对三种大小的叶子都能读，1 GiB、2 MiB、4 KiB 的叶子在它眼里一样大，走到哪一级叶子就把哪一级的物理字段取出来拼上低位偏移。还有一件事发生在类的外面：TLB 的维护被有意留在了走表之外，头文件注释的理由是走表只改表，翻译的事归调用者管，因为知道哪个世界正在跑的只有调用者。我们看 cow 的代码就能对上，MarkPageShared 改完了表项，自己把 InvalidatePage 调了，把这个地址的翻译作废了。

## 切换：activate 与调度器的接缝

activate 的本体只有一句，我们一眼就能看完：`root_` 不为 0 的时候，它就把自己的物理地址装进 CR3。头文件的注释提了一个条件，切换的前后中断都得关着，靠的正是上半的镜像让调用者在新世界里活着，想回去的话，`LoadCr3(KernelPageRoot())` 就是回家的路。

日常的切换并不经过 activate，我们看调度器那边的接缝。task_runtime.cpp 的 arm_task_entry 在每次交接的时候问任务要 `user_root`，有用户世界的任务就装它的根，没有的就装内核的根，装之前它还把 CR3 比对了一遍，已经对了的话就不重装，省掉一次不必要的刷表。回 Main 的时候，return_to_main 也做同样的比对，把内核的根装回去。也就是说地址空间的切换点在上下文切换里，任务带着自己的根上 CPU，退下来的时候机器回到内核的世界。

execve 走的是另一条路，也是我们最要小心的一站。sys_exec.cpp 里造了一个全新的 AddressSpace，让加载器把 ELF 的段一页一页铺进去，然后才轮到换世界的事。换的写法上有讲究，源码里的注释说，正在退场的地址空间不能在 CR3 还踩着它们的时候就散架，所以旧的 space 和 vmas 两个持有被挪到局部变量里停一停，activate 一完成新的根就在跑了，等作用域收尾、引用计数归了零再释放，拆除这时才是安全的。fork 那边的做法简单得多，BuildForkChild 调 cow 的 CloneSharedSpace 造出孩子的世界，根记进了 `user_root`，持有挂上了 `space`，孩子被调度到的那一刻就自动住在自己的世界里了。

## 克隆与退场：计数看住每一帧

我们跟着 CloneSharedSpace 把父亲的用户半边走了一遍，每一个 4 KiB 的叶子都按同样的地址映进孩子的世界，然后两边一起标成了共享：写位摘掉了，软件的 kCow 位（写时复制留在页表项里的软件位）立了起来，帧的使用计数加了一。设备区的待遇不一样，cow.cpp 里的判断看到 VMA 的类别是设备就直接返回，不参与共享的标记。遍历完了还有一处细节，父亲此刻正被 CR3 踩着的话就重装一次自己的根，把改过的表项的翻译刷掉。写时复制引擎的头文件注释交代了它存在的理由，那是注释自己的说法，引擎更早的版本没有这个计数，父亲退出的时候会把孩子还在读的帧放掉，现在每一个叶子的归还都过 ReleaseFrame，还有伴的帧原地不动，最后一个使用者才把它还到分配器的手里。

写错误落进页错误的路径时，接手的是 TryResolvePageFault，我们看它怎么处置。还有同伴的共享页拿到一个新帧、内容复制了一份，最后一个共享者直接把写位要了回来，两边也就不用再走一遍映射的手续。cow.hpp 的注释里写明了纪律，全部在页错误的深度上完成，睡眠是不许的，除了一个帧没有别的分配，锁只借 PMM 自己的那几把。不过代码实际比注释多走了一道：TryResolvePageFault 一进门就有 IrqGuard 关掉了中断，每一笔写时复制的处理还要过 cow 引擎自己的 g_fault_gate 故障闸，它的真身是 cow.cpp 顶上的一把自旋锁（SpinMutex）。MarkPageShared 与 ReleaseFrame 取闸的时候，走的也是这一把。注释声称的与代码实做的，我们都原样摆在读者面前，出入我们不藏也不替哪一头背书。

teardown 是这个类一生里的最后一站，我们把它的顺序走清。它只动用户的半边，上半边是内核的镜像，那些帧本来就是内核的，我们碰都不碰它。256 个用户表项里每一个 present 的，我们都递归往下走：叶子帧交给 `cow::ReleaseFrame`，让计数决定这一页是不是真的能还，中间的表按层释放回 PMM。途中遇上带 kLarge 位的表项，Check 当场把机器拦了下来，理由是用户半边的设计里没有大页的位置，凡是映用户内存的代码都不造这样的东西。轮到根自己的时候，它也归了零，空间回到了 init 之前的形状。析构就是 teardown 的 RAII 壳，已经拆过的再析构只是空转。注释里的红线我们要原样转述，拆除进行的时候，任何一个 CPU 的 CR3 都不能还踩在这个世界上，exec 把旧的持有停到切换之后才放，守的就是上面说的红线。

## 源码位置

- `kernel/mm/address_space.hpp`、`kernel/mm/address_space.cpp` — AddressSpace 类、init、map、map_user、translate、activate、teardown
- `kernel/mm/page_walk.hpp` — 四级走表的共享模板，TableWorld 概念与各级工具
- `kernel/mm/vmm.hpp`、`kernel/mm/vmm.cpp` — KernelTables 世界、KernelPageRoot
- `kernel/mm/cow.hpp`、`kernel/mm/cow.cpp` — CloneSharedSpace、MarkPageShared、TryResolvePageFault、ReleaseFrame
- `kernel/proc/task.hpp` — Task 的 space 与 user_root 字段
- `kernel/proc/task_runtime.cpp` — arm_task_entry 与 return_to_main 里的根切换
- `kernel/proc/fork.cpp` — BuildForkChild 里孩子世界的建立
- `kernel/syscall/sys_exec.cpp` — execve 造新世界与旧持有的停放
- `base/include/cinux/ref_count.hpp` — RefCount 侵入式计数
- `test/kernel/test_cow.cpp`、`test/kernel/test_sched.cpp` — 克隆、共享与归还的 host 测试
