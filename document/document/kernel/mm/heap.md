# 堆管理

## 功能概述

这一页我们跟着一次 `new` 的旅程，把内核的堆子系统完整走一遍。堆管的是比页更小的那层分配：物理页归 PMM（物理内存管理器，管物理页的分配和归还）管，虚拟区间的事归 VMM（虚拟内存管理器）管，两者各自有单独的一页可以去看，各页的名字是 pmm.md 与 vmm.md，而代码里到处要的 `new` 一个对象、开一块缓冲区，落到的就是这里。实现分成了两层，`Heap` 这个类只懂的是算术，一段连续虚拟内存怎么切成块、怎么记空闲、怎么合并，全是它的话，页表它是一个字都不碰的。把页真正映射进来、再把全局 `new`/`delete` 接过来的，是 runtime 层的职责。这么拆的理由在头文件的注释里写得明白：不碰页表的代码可以直接链进 host 测试，`test/unit/test_heap.cpp` 拿一块普通的缓冲区就能把分配语义整个验证一遍，同一个类在内核里跑得通了，在测试的缓冲区里也照样是跑得起来的。

## 块的形状：Header、Footer 与空闲链

堆的世界被切成了一串首尾相接的块，每块的头部 32 字节，装着四个字：payload 记的是载荷字节数，state 记它现在的状态是空闲还是在用，next 是空闲链的下一环（在用的块把这个字段闲置着），spare 目前是空着的。块尾还有 16 字节的 footer，里面写的是整块的总长，也就是头加载荷加尾的和。我们为什么愿意为每个块多付这 16 字节？因为 free 要问的问题是我前面那块邻居是谁、它空不空，footer 从后往前一读就答出来了，不需要再去维护一个双向的链表。整块最小也有 64 字节的下限，也就是头 32 加尾 16 再加最小载荷 16 的总和，载荷一律是按 16 字节对齐的，不足的补齐，所以 `allocate` 交回来的指针拿去做 SSE（一组要求数据对齐的指令，地址歪了 16 字节就要崩）都不慌。

空闲块额外串成了一条按地址排序的链，我们把链头存在 `free_head_` 里。排序不是白排的：first-fit 从链头找到了第一个装得下的块，拿到的就是地址最低的那个洞，小块尽量地留在原地复用，分配出来的地址也因此更稳定。这些结构定义都收在 `heap.cpp` 的匿名命名空间里，算是纯粹的实现细节。外面的世界只见 `Heap` 的四个动词，对应的正是 init、allocate、free、grow。

## allocate：first-fit 加分裂

```cpp
// kernel/mm/heap.cpp
void* Heap::allocate(unsigned long bytes) {
    const arch::IrqGuard kIrq;
    const auto           kLock   = lock_.guard();
    unsigned long const  kWanted = round_payload(bytes);
    unsigned long const  kBlock  = pick_free_block(free_head_, kWanted);
    if (kBlock == 0) {
        return nullptr;
    }
    unlink(&free_head_, kBlock);
    Header* const       kHeader    = header_at(kBlock);
    unsigned long const kOld       = kHeader->payload;
    unsigned long const kRemainder = kOld - kWanted;
    if (kRemainder >= kHeaderBytes + kFooterBytes + kMinPayload) {
        unsigned long const kTailPayload = kRemainder - kHeaderBytes - kFooterBytes;
        unsigned long const kTail        = kBlock + span_bytes(kWanted);
        make_free_block(kTail, kTailPayload);
        link_sorted(&free_head_, kTail);
        kHeader->payload = kWanted;
        write_footer(kBlock, kWanted);
        free_bytes_ = free_bytes_ - kOld + kTailPayload;
    } else {
        free_bytes_ -= kOld;
    }
    kHeader->state = static_cast<unsigned long>(BlockState::kLive);
    return cinux::base::PtrAt<void>(kBlock + kHeaderBytes);
}
```

我们按顺序读它做的事。进门的时候关中断、拿自旋锁，堆是全内核共享的状态，这两道检查是我们一道都省不得的。要的字节数向上对齐到 16 之后，first-fit 沿着空闲链扫了过去，找到第一个够大的块。一个都找不到的话它就返回 nullptr，要注意的是它自己不去扩堆，扩堆是 runtime 层的事，这个分工我们放在下面一节细讲。找到的块如果比要的大出足够多，也就是剩下的部分还得装得下头、尾加最小载荷，尾部就分裂出一个新的空闲块挂回链上，头部改成了小的，footer 重写了一遍。剩不下那么多的话整块给你，内部的一点零头不算浪费。最后状态翻成了 kLive，返回的指针跳过头部，正好落在 16 对齐的载荷上。找不到洞返回 nullptr 而不是就地停机，这个决定也是接缝的一部分，失败要留给上层的 `HeapAllocate` 去补救。

## free：校验加双侧合并

```cpp
// kernel/mm/heap.cpp
bool Heap::free(void* block) {
    const arch::IrqGuard kIrq;
    const auto           kLock  = lock_.guard();
    unsigned long const  kBlock = reinterpret_cast<unsigned long>(block) - kHeaderBytes;
    if (kBlock < base_ || kBlock >= top_) {
        return false;
    }
    Header* const kHeader = header_at(kBlock);
    if (kHeader->state != static_cast<unsigned long>(BlockState::kLive)) {
        return false;
    }
    unsigned long const kSpan = span_bytes(kHeader->payload);
    if (kBlock + kSpan > top_ || kBlock + kSpan <= kBlock) {
        return false;
    }
    unsigned long       merged = kHeader->payload;
    unsigned long const kNext  = kBlock + kSpan;
    if (kNext < top_ && header_at(kNext)->state == static_cast<unsigned long>(BlockState::kFree)) {
        unlink(&free_head_, kNext);
        merged += kHeaderBytes + kFooterBytes + header_at(kNext)->payload;
        free_bytes_ += kHeaderBytes + kFooterBytes;
    }
    unsigned long target = kBlock;
    if (kBlock > base_) {
        unsigned long const kPrev = kBlock - read_prev_span(kBlock);
        if (kPrev >= base_ &&
            header_at(kPrev)->state == static_cast<unsigned long>(BlockState::kFree)) {
            target = kPrev;
            merged += kHeaderBytes + kFooterBytes + header_at(kPrev)->payload;
            free_bytes_ += kHeaderBytes + kFooterBytes;
        }
    }
    unsigned long const kReturned = kHeader->payload;
    header_at(target)->payload    = merged;
    header_at(target)->state      = static_cast<unsigned long>(BlockState::kFree);
    write_footer(target, merged);
    if (target == kBlock) {
        link_sorted(&free_head_, target);
    } else {
        kHeader->state = static_cast<unsigned long>(BlockState::kFree);
    }
    free_bytes_ += kReturned;
    return true;
}
```

进门的是三道校验，我们把它们拦的东西一条条看过去。指针退回头部的位置，出界的那个不收。state 不是 kLive 的不收，所以同一块交两遍的时候，第二遍在这里就答了 false，double free 在这层的下场就是这样。长度算出来越界或者回绕的也不收。校验全过了才动真格：右边的邻居紧挨着而且空闲，我们就把它从链上摘下来并进来，左边的邻居靠 footer 反推地址，空闲的话也并进来。我们每并掉一块，free_bytes_ 就多回了头加尾的一份开销，碎片就这样一块块地合拢。合并完成后以 target 为准重写了头和 footer，原本就在空闲链上的块不用重新挂链，改一下 state 就好了。

值得留意的是这层 free 的返回值语义：false 在我们手里不一定是 bug，指针压根不属于这个堆的、double free、长度异常，都会答的是 false。判定权交还给了调用者，`HeapFree` 原样转达了判定，到了全局 `delete` 那里才把它升级成 panic。堆类自己不炸的理由，是我们想让 host 测试能把拒绝路径一条条断言到位。

## grow 与自动扩展的分工

`Heap::grow(new_top)` 把堆的顶端往上抬。新的空间被做成一个空闲块，尾巴上那块如果也是空闲的，就地并进去，省下了一个块位。它身上的检查我们一眼就看得清：新顶得高过旧顶、得按 16 对齐、抬升量得装得下一个最小的块，不然它答的是 false，grow 的入参条件就是这样卡着的。

页表的事全在 `heap_runtime.cpp` 这边，开机走的是 `BringUpHeap`。layout.hpp 里把 `kHeapBase` 定在了 `0xFFFF808000000000`，紧跟在了直接映射区的后面，占的是下一个 PML4（四级页表的顶层）表项，vmalloc 区又排在堆的后面。layout.hpp 里的两条 static_assert 我们都看过了，这个次序在编译期就确定了下来。起点还要往下让出 4 KiB 的下界守卫页（`kHeapLowerGuardBytes`），谁写穿了堆底，炸的就是守卫页的映射，而不是别的数据。首批映射的是 `kHeapInitialBytes`，也就是 64 KiB 的空间，映射完交给了 `Heap::init` 整块开张。它在 init_sequence 的 heap 站位运行，排在地址空间就位之后，因为页行走的是正式的布局，供它查表的元数据也得赶在它前面装好。我们读这里的装配时，把这个次序记在心里也就是顺手的事。

运行中的扩展在 `HeapAllocate` 里，我们把这段两层合作的写法读一遍：

```cpp
// kernel/mm/heap_runtime.cpp
void* HeapAllocate(unsigned long bytes) {
    const cinux::arch::IrqGuard kGuard;
    const auto                  kLock = g_heap_gate.guard();
    if (void* const kBlock = Heap::self().allocate(bytes)) {
        return kBlock;
    }
    unsigned long const kPages  = cinux::base::math::Ceil(bytes, kSize) + 1;
    unsigned long const kGrow   = kPages * kSize;
    unsigned long const kOldTop = Heap::self().top();
    if (kOldTop == 0 || !map_heap_pages(kOldTop, kGrow) || !Heap::self().grow(kOldTop + kGrow)) {
        return nullptr;
    }
    return Heap::self().allocate(bytes);
}
```

分配在这里落了空的话，我们按要的字节数向上折成整页、再多备一页，从旧顶开始逐页向 PMM 要物理帧、经共享的页行走映射进当前的页表，然后把新的顶端报给 `grow`，算术层把新顶端做成了一个空闲块、挂回了空闲链，末了重试一次分配。多备的那一页是给块的头尾留的余量，不然一次刚好按载荷折页的扩展，头尾的开销会把请求卡在外面。物理帧要不到、映射失败了，答的就是 nullptr。这段代码和 `Heap` 类分居两个文件的理由，正是整套设计里我们最认的原则：懂页表的和懂算术的互不越界，host 测试便可以只带上算术的这半边玩。

## 全局 new 与 delete

```cpp
// kernel/mm/heap_runtime.cpp
void* operator new(unsigned long bytes) {
    void* const kBlock = cinux::mm::HeapAllocate(bytes);
    cinux::base::safety::Check(kBlock != nullptr, "kernel heap exhausted");
    return kBlock;
}

void operator delete(void* block) noexcept {
    if (block == nullptr) {
        return;
    }
    cinux::base::safety::Check(cinux::mm::HeapFree(block), "invalid heap free");
}
```

全局的 `new`/`new[]` 和四个 `delete` 变体全都改道了 `HeapAllocate`/`HeapFree`，内核里随手写下的一个 `new KObject`，落到的就是这套堆。两边的态度不一样，我们分开说。分配失败在这里是不留活口的，Check 直接停机报了“kernel heap exhausted”，因为空指针解引用在内核里是更难查的错，所以宁可当场停机，把缺的东西报清楚。删除的态度就不一样了，空指针安静地放行，这是语言标准的要求，判定失败了才升级成“invalid heap free”的 panic，double free 和野指针在门口就被点了名。runtime 这层自己还加了 `g_heap_gate` 这把自旋锁，和 `Heap` 内部的锁叠成了两道，`HeapAllocate` 的一段调用里既要动类内的链、又要动页表和 PMM，两把锁各守各的临界区。还有一件事照实记：旧文档里的 `alloc(size, align)` 和带 `align_val_t` 的对齐版 operator new，在当前的实现里已经移除了，现在的对齐手段，只剩 allocate 内部固定到 16 字节的了。

## 线程安全

堆是全内核共享的，锁的纪律我们数了一遍。`Heap` 的类内有一把 `SpinMutex`，init、allocate、free、grow 四个动词走的是这同一把锁，free_bytes() 与 top() 两个查询口走的也是同样的锁，全都套在 IrqGuard 的组合里，关了中断再拿自旋锁，谁在哪个核上都是不能半路被切走的。runtime 层的 `g_heap_gate` 再罩住了分配、扩堆、重试的整段调用，让扩堆期间的页表操作不会和另一个核的分配咬在一起。取舍上照实说：这是一把全局的 SpinMutex，所有入口走的是同一把锁，多核同时分配的话就会在这里排队。

## 设计取舍

最后我们把这一页里做过的换法拢一拢。first-fit 只扫到第一个够大的块就停了，写出来的代码短，地址最低的洞总是头一个被复用，分配出来的地址也就稳。长时间运行后低地址可能攒下难用的小洞，双侧合并会把相邻的空闲块拼回去。footer 的 16 字节里存着整块总长，free 靠它从后往前读出了前一块的位置，所以省掉了一个双向链表。算术与页表的分层是最重的一条：它让同一个堆类同时活在内核和 host 测试两个世界里，拒绝的路径、合并的路径都能在普通机器上逐条断言。还有一点我们要照实记：堆类拿不出 `dump_stats` 这样的调试口，现在的观测面只有 `free_bytes` 和 `top`，要更细的洞分布得自己去读内存，剩下的我们留给以后。

## 源码位置

- `kernel/mm/heap.hpp`、`kernel/mm/heap.cpp` — Heap 类：init、allocate、free、grow 与块算术
- `kernel/mm/heap_runtime.hpp`、`kernel/mm/heap_runtime.cpp` — BringUpHeap、HeapAllocate、HeapFree、全局 new/delete
- `kernel/mm/heap_config.hpp` — 首批映射字节数与下界守卫页常量
- `kernel/mm/layout.hpp` — kHeapBase 与堆、直接映射、vmalloc 的表项次序
- `kernel/init/init_sequence.cpp` — bring-up 里 heap 一站的启动顺序
- `test/unit/test_heap.cpp` — 堆语义的 host 测试，六个用例：init 开张、分配对齐与 free_bytes 的数值、free 合拢、first-fit 复用、拒绝路径、grow
