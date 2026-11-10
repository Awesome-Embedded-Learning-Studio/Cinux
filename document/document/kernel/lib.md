# 内核库

## 功能概述

这一页我们把内核的库层走一遍。今天它分在了两处，`base/include/cinux` 的头文件里住着公共件，`kernel/lib` 的下面只剩两份内核自己的定义。公共件管的是格式化、容器、位运算、内存原语与所有权计数这一类底料，引导件、内核与 host 测试三个世界引用的是同一套。两份内核定义管的是断言钩子与 libc 拼写的内存函数，它们只活在内核的镜像里。旧页写的 kprintf 多后端、`kernel/lib/string.hpp` 与自实现的原子文件在现在的树里都找不到了，打印如今走 base 的格式化引擎，原子直接用工具链的 `<atomic>`，本页按现在的源码重写。

两边的界线我们用一句话说清。base 的件不认得内核，其实也不认得任何的操作系统，而编进哪个世界就在哪个世界里活着，绝大多数的件是头文件内联，真正的编译单元只有格式化引擎、解析与路径这几份，各世界按自己的名单领走。两份内核定义没有自己的头文件，声明全部住在了 base，链接的时候按世界挑各自的定义，host 世界挑的是 `test/framework` 里的孪生文件。base 的消费者我们数过一遍，网络、文件系统与系统调用的源文件里几乎处处是 `result.hpp` 的身影，内存原语、指针工具、位运算与容器的引用也铺得很开。

用法的故事我们交给了兄弟页，本页认的是库本身的设计与边界。Bitmap 在物理内存管理里怎么当页号的底册，[物理内存管理](mm/pmm.md) 那一页已经走过了。锁与等待链的用法归 [同步原语](proc/sync.md)。SelfList 与 StaticStack 的用法这一页在下面自己讲，类注册的用法在 [进程与调度器](proc/process_and_scheduler.md) 那一页里写过了。断言炸了之后的走读分了两个方向，异常那半的走读归 [x86_64 架构层](arch.md)，内存那半的走读归 [虚拟内存管理](mm/vmm.md)，本页讲到停机的那一下为止。

## 内核侧的定义

`kernel/lib` 的目录里今天只有两个编译单元，`mem.cpp` 管的是内存函数，`assert.cpp` 管的是断言钩子，连一个自己的头文件都没有。这样的形状是接缝决定的，我们把分工摆在这里，声明住在 base 的 `memory.hpp` 与 `assert.hpp` 里，定义归的是各世界，链接的时候才轮到认领，所以这个目录里放什么，看的正是内核世界要替哪些名字交答案。

我们看 `mem.cpp`。文件里的注释把它的身份写了出来，这些是 libc 拼写的符号，编译器可能不经我们的手就发出对它们的调用，哪怕我们的代码一个 `memset` 都没写，链接器也找得到它们的定义。函数的名单是五个 C 名字（`memset`、`memcpy`、`memmove`、`memchr`、`memcmp`），全部用 `extern "C"` 的旗号摆出来，头两个把 base 的 `CopyBytes` 与 `SetBytes` 包成了 C 名字的入口，重叠规则更讲究的活留给了 `memmove`：

```cpp
// kernel/lib/mem.cpp
void* memmove(void* destination, const void* source, unsigned long bytes) {
    auto*       out = static_cast<unsigned char*>(destination);
    const auto* in  = static_cast<const unsigned char*>(source);
    if (out < in || out >= in + bytes) {
        cinux::base::CopyBytes(destination, source, bytes);
        return destination;
    }
    for (unsigned long index = bytes; index > 0; --index) {
        out[index - 1] = in[index - 1];
    }
    return destination;
}
```

重叠的判断我们停一步看。目标区间落在源头的右边或者不挨着的时候，正向拷不会踩到还没读过的字节，`memmove` 把活交给 `CopyBytes` 就完事了。目标区间压进源头中间的那一次，它是逐字节倒着走的，从最末尾的那个字节往回搬，源头还没被覆盖的部分就完好地留到了被读的时刻。剩下的 `memchr` 与 `memcmp` 两个是朴素的手写循环，一个管的是找字节，一个管的是比字节，比较答的是 -1、1 与 0。

文件的末尾还有一个不显眼的落点，`std::terminate` 的定义安在这里。异常在这个内核里是关着的（编译旗标 `-fno-exceptions`），可标准库的头文件里依然留着 throw 的字样，真被走到的那一刻需要一个去处，注释把这层写成了 freestanding（不带操作系统与完整标准库的编译环境）世界对头文件的供给，这里的供给是 `Halt()`，机器当场就停了下来。还有一个编译事实我们照实记录，`kernel/CMakeLists.txt` 把 `mem.cpp` 放进了无 SSE 的孤岛名单，整份文件是按 `-mgeneral-regs-only` 编译的，向量寄存器不在它的世界里。

引导世界还带着自己的一份 `boot/early/libc.cpp`，里面是 `strlen` 与 64 位除法的两个 C 名字落点，它的名字也在内核镜像的编译名单里。不过它的服务对象是引导的早期阶段，那一层归 [引导链](../boot.md) 的故事，这个部分是本页没有认领的，我们把它摆出来，图的只是边界的完整。

## 断言的接缝

断言的声明住在 base，`assert.hpp` 文件头的注释把整个安排讲完了，`Check`（这个内核的运行时断言，命中了就停机报告）的本体在头文件里内联，`AssertionFailed` 的定义是各世界自己的事，每个世界交自己的定义，挑的动作发生在链接的时候，调用点在两个世界里是一字不改的。我们把声明摘出来看：

```cpp
// base/include/cinux/assert.hpp
inline void Check(bool condition_result, const char* k_message,
                  std::source_location loc = std::source_location::current()) {
    if (condition_result) {
        return;
    }

    AssertionFailed(k_message, loc);
}
```

内联是刻意的，我们听注释讲的理由，那道转发分支要留在调用点的位置上，默认参数的 `source_location` 才能抓到真调用处的文件与行号，收进单独的 .cpp 里的话，抓到的就永远是转发的位置了。`AssertionFailed` 的身上标着 `[[noreturn]]`，谁被调用了谁就不回头。

内核世界的定义在 `kernel/lib/assert.cpp`，我们一起读它。`Check` 在内核里命中是不可恢复的事，文件头的注释就是这么写的，打印一行 panic 的原因，带上抓到的位置，然后把机器停了下来，不再碰任何的状态。函数体干的就是这几步：

```cpp
// kernel/lib/assert.cpp
void AssertionFailed(const char* k_message, std::source_location loc) {
    cinux::print::Println("[panic] %s", k_message);
    cinux::print::Println("[kern] %s:%u in %s", loc.file_name(), loc.line(), loc.function_name());
    unsigned long long frame = 0;
    __asm__ volatile("movq %%rbp, %0" : "=r"(frame));
    cinux::debug::PanicDump(frame);
}
```

`Println` 报的是原因与位置，帧基址是一条内联汇编抓来的 rbp，剩下的部分交给了 `PanicDump`，帧回溯、当前任务与内存概貌都是它的活。走读的分工是这样摆的，异常那半的走读在 [x86_64 架构层](arch.md) 里，内存那半的走读归 [虚拟内存管理](mm/vmm.md)，它自己的本体住在 `kernel/debug/panic.cpp`。滥用的样例我们手里也有一个，对没有所有者的引用计数再加一次，`RefCount` 的 `Check` 当场就是停机报告。

host 世界的孪生在 `test/framework/assert_host.cpp`，声明是同一份的，定义换成了往 stderr 打一行位置信息再调用 `abort()` 的组合，共享声明那一侧的注释特意交代了 host 的输出不缓冲，为的是让断言消息抢在进程死掉之前到达终端。两个世界怎么各拿各的定义，我们在讲 host 测试的那一段里把链接名单摆出来，这里要看的是一个特别的名字。

`assert.cpp` 的末尾还站着一个 libstdc++ 的落点，`GlibcxxAssertionFailed`，管的是标准库头文件里自带的断言在内核里的去处。它的符号名是硬编码的汇编名（`_ZSt21__glibcxx_assert_failPKciS0_S0_`），命中了之后的动作照样是打印与停机。注释里作者自己留了一条 TODO，写的是想让编译器生成这个名字而不是手写，我们把这层现状原样带给大家。

## 格式化引擎与打印链

引擎的公共面在 `base/include/cinux/format.hpp`，编译单元是 `base/src/format` 下的三份，我们把设计的立场摆出来。参数不走进 C 的变长槽位，全部装进的是普通的结构体，注释把理由写在了头上，`-m16`（让编译器按 16 位实模式出引导代码的旗标）的引导世界里调用方与 `va_arg` 对不上槽位的宽度，C 变长在那边是可用不了的，普通结构体的传递约定在哪个世界都一样，格式化才敢放心地跟着引导链走。

```cpp
// base/include/cinux/format.hpp
struct Arg {
    enum class Kind : uint8_t {
        kUnsigned,  ///< Read as an unsigned integer.
        kSigned,    ///< Read as a signed integer.
        kString,    ///< Read as a NUL-terminated string pointer.
        kPointer,   ///< Read as a pointer.
    };

    Kind     kind;   ///< How the value was intended to be read.
    uint64_t value;  ///< Raw bits: integer value or pointer address.
};
```

装进的都是同一个 64 位的 `value`，数值与指针在它这里只有位的差别，读法由 `kind` 的值来定，打包靠的是 `NumU`、`NumS`、`Str`、`Ptr` 四个包装函数，视图的得来是 `ArgsView::of` 对栈上数组数一遍的事。入口的数目是两个，我们分开认：`FormatToBuf` 吃的是 `ArgsView`，哪个世界的代码调它都是合法的。`VformatToBuf` 吃的是 C 变长参数，头文件把它标成了 host 限定，内核镜像的源文件名单里没有 `entry_variadic.cpp` 的名字，变长糖在内核这边是用不上的。

真正的解释器是 `detail.hpp` 里的 `RunEngine`，它的形状是一个模板，对参数源的要求是五个取值动作（取字符、取字符串、取有符号数、取无符号数、取指针），输出走的是一张 `Sink`，它是一个函数指针加一个上下文指针的组合，每一个产出的字符都当场交出去。支持的面我们照头文件的注释列全：`%%`、`%c`、`%s`、`%d`、`%u`、`%o`、`%x`、`%X`、`%p`，宽度修饰认的是 `%Nd` 这样的写法，补零认的是 `%0Nd`，左对齐认的是 `%-Nd` 与 `%-Ns` 的写法，长度修饰的候选是 `l`、`ll` 与 `z` 三种，解析出来的结果交给参数源解释。不认识的转换符会被原样回显成百分号加那个字符，注释里没有给它安排过更聪明的去处。

数字到字符的转换住在 `radix.cpp`，我们挨个看。`FormatRadix` 把无符号值按 2 到 16 的任一进制转成字符，零的转出结果是单个的 0。`FormatDecimal` 管的是有符号十进制，`INT64_MIN` 是被特判的，注释给的理由很直接，补码里对它取负是会溢出的，所以干脆把字面的字符串复制了进去。`%p` 的输出带 `0x` 前缀、十六进制小写，走的是 `FormatPointerPrefixed`，补零在指针这一档是被强制关掉的。装中间结果的缓冲是栈上一个 24 字节的小数组，头文件推荐的最小值就是它。

写入的纪律我们单独念一遍，判断式是 `pos + 1 < capacity` 的形状，写的位置永远给结尾的 NUL 留了一个位，装不下的时候是静默截断，缓冲区的指针若是空的，整个调用就等于什么都没有发生。另一个细节把两头连在了一起，`FormatToBuf` 答的是 void，长度是它不报的，所以 `memory.hpp` 里备了一个带上限的 `StrLength`，注释说它的日常调用者就是拿它来量格式化写进去的字数的。

内核自己的打印糖住在 `kernel/boot/print.hpp`，我们把它的两件事认完。`Print` 与 `Println` 的本体是模板，`Argify` 的三个重载（无符号长整、字符串、指针）在编译期把参数包成 `Arg` 数组，注释把这一点说清楚了，模板不进 C 变长的槽位，所以 `-m16` 的世界照用不误。缓冲是栈上一个 192 字节的定长数组（常量 `kPrintBufferSize`），发出去的活归 `console::PutString`。

打印链的最后一程在 `kernel/console/console.cpp`，我们跟着它走到设备的那一头，`PutString` 的手里是中断守卫加一把 `SpinMutex`，一个字符一个字符地走，串口有它的一份，VGA 的文本台也有它的一份。这一把锁是 base 的 `SpinMutex` 在内核里的消费样例，互斥是它管的，中断的开关归旁边那个守卫管，头文件的注释把分工写明了，中断政策留给的是调用者。库本身没有可讲的初始化函数，旧页里 `kprintf_init` 那样的站位已经不存在了，`kernel.cpp` 的 Main 在 `RunInitSequence` 返回之后才开始打日志，初始化序列的头一步就是串口，文本台的初始化排的是 console 那一步，日志出来的时候，两条路都已经是活的。

## base 的容器

六个容器我们挨个认。它们的身份全是模板，内存是它们不分配的，并发的事一律不管，锁是谁用谁自己带上的，这几条立场的字样每个头文件里都有。

我们从 `self_list.hpp` 认起，它的形状是侵入式的单链 FIFO，来历写在头上的注释里，借自 ZerOS 的同名文件，砍剩的是这个内核今天要用的四个动作（尾插、头插、弹出头、摘除）。侵入式形状的意义注释讲了，节点长在对象的身上，停一次车不花分配的功夫，一个对象也上不了两条同基的链，这正是等待链要的不变量。链节是私有的，能碰它的只有链自己，注释补的那句把后果点到了，绕开链去接线的手正是就绪队列悄悄坏掉的缘由。一份对象要同时坐上几条链的时候，走的是成员链接的路子，`SelfList` 的第二个模板参数收一个成员指针，像 `&Task::child_link` 这样的写法，链改骑的就是成员节点，基类节点与每个成员节点各是各的线，调度器手里的四条名册正是这么骑的。`size()` 是故意走 O(n) 的，注释的理由是侵入链的成员可以从别的链手里进出，缓存的计数迟早会跟真实状态漂移，诊断用的次数少，走一遍是诚实的代价。

我们接着认 `static_stack.hpp`，它的形状是定容量的栈，存储是内联的 `std::array`，构造是常量的，freestanding 的单例里放得下。`push` 与 `pop` 的回答都是布尔，满了、空了都答假，弹出的元素会被复位成默认值，注释把这层行为写明了。查看的面是 `view()`，给出来的是自底向上的 `std::span`。调度器拿它装的是类名册，容量是四个调度类的上限，类注册的用法在 [进程与调度器](proc/process_and_scheduler.md) 那一页里写过了。

我们看 `slot_table.hpp`，它管的是下标稳定的定容表，存储的底子是一个 `std::optional` 数组，插入挑的是最低的空位，占了的下标终生不变，别的槽位是不跟着动的。它的插入口里有两个值得认一下，`insert` 挑的是最低的空位，`place` 按的是调用者给定的下标，注释举的场景是 dup2 的目标描述符，编号是调用者自己的家当，就得落在指了名的位置上。文件描述符表与 PCI 的槽位都 include 了它，锁依赖检查器的名单里也有它。

我们再看 `ring_queue.hpp`，它的形状是定容的环形队列，容量里永远留的是一个空槽，满与空的界线才分得开。溢出的政策注释写得很清楚，队列满的时候 `push` 答的是假，新来的元素就地被丢下，队里住着的元素原样不动，要不要重试才是调用者的事。`peek` 能隔着下标看一眼而不取走。键盘驱动的事件行、TCP 的收发缓冲、管道的字节流、回环网卡的包，消费的都是它。

我们认 `name_table.hpp`，它的全部是 `FindByName` 与 `FindByWord` 两个查找函数，两个都是 constexpr 的查找，找的时候对着常量数组按名字或按字键，找到的时候给的是指针，找不到给的是空。消费它们的两位也各有各的差事，devfs 的节点文件拿 `FindByWord` 按 ioctl 的字键查答案表，procfs 那一侧拿 `FindByName` 查固定名字的表，devfs 的根名册用的是自己写的 `same_name` 比较。`directed_graph.hpp` 的形状是一个不分配的有向图，顶点的上限是 64 个（一个机器字的位数），边装在邻接的位掩码里，`reaches` 做的是带环检测的可达查询，全程用的是有界的局部掩码，递归是它不碰的。锁依赖检查器拿它当的是等待图。

## 位运算与位图

位运算的词汇我们分三层认。`bit_ops.hpp` 的内容是命名原语的清单，管的是低半字节、高半字节、低字节这样的切片动作，写法都是 constexpr 的模板。`bitmask.hpp` 给出的是一个带类型的字，`BitRange` 描述的是一个位域的位置与宽度，`BitMask<T>` 在公开的 `raw` 身上铺了一层具名的置位、查询与存取词汇，注释强调 `raw` 的宽度在哪个编译世界都不变，这样的字数组跟硬件视角的同一张表是字节兼容的，页表、GDT 与 PCI 的寄存器词汇都铺在这上面。

`bitmap.hpp` 的 `Bitmap` 是不自带存储的位图，字数组归的是调用者，分配是 base 不做的。容量的单位是位，字数的换算归 `words_for`，尾巴上装不进整字的那些位靠掩码挡住，全置位与全清位的操作不会越过容量装样子。两个绑定的入口我们把分工念一遍，`init` 的工作是把位清成白纸，`adopt` 接手一份已经有意涵的位图，注释举的场景是从盘上读出来的位，接手的时候它会顺着字把已置位数走一遍，为的是让后面的置位清位维持住诚实的计数。找空位的 `find_clear_run` 整字跳过全置位的字，空位的计数按位来，数够了的时候交回起点，找不到交回的是 `kInvalidIndex` 这个哨兵。已置位数与已清位数是现成可问的，PMM 的空闲页计数正是拿容量减它得来的，页号怎么换算成物理地址，[物理内存管理](mm/pmm.md) 那一页讲过了。

## 字节、内存与所有权

我们从 `memory.hpp` 认起，它的原语是字节级的搬与填，拷的活归 `CopyBytes`，填的活归 `SetBytes`，`BytesAre` 问的是一段内存是不是同一个字节，`EqualBytes` 比的是两段内存。注释给它们定的性是几个世界共用的轮子，头文件的内联让每个世界在实例化的时候白拿一份，也没有哪个编译单元持有自己的状态。`StrLength` 是带上限的 `strlen`，注释说墙是它的意义所在，格式引擎答的是 void，调用者想知道写进去的字数，量的时候不许越过上限。

所有权的三件套住在 `own.hpp`、`shared.hpp` 与 `ref_count.hpp`，我们一件一件认。`Own` 的身份是唯一的持有者，移动是允许的，拷贝是不存在的，析构的时刻就是释放的时刻，负责造的入口是 `MakeOwn` 与 `MakeOwnArray` 这一对。`Shared` 与 `Weak` 服务的对象是带着引用计数的，多出来的每一份持有都来自一次拷贝，最后一个放手的人负责回收，`Weak` 观察用的锚点住在对象身上，对象走的时候 `vacate()` 把所有观察者摘干净。`RefCount` 的本体是原子计数，生下来的初值就是 1，计数的变化走的是 acquire/release 的配对，加计数的动作是 `retain`，减计数的动作是 `release`，零之后是不许复生的，注释把这层写成了硬边界，滥用会在 `Check` 的关口被当场拦下，对没有所有者的计数再加一次、加到上限再加一次、没有持有时释放，这样的情形在它的眼里全是停机级的事件。管道、套接字、VMA（任务地址空间里的一段区域）、打开的文件描述，这些消费者的身上都骑着它。`text.hpp` 的 `CopyText` 是最后一件小的，往调用者的存储里拷一个截断的前缀并补上 NUL，拷了几个字它照实回答。

## 单件、守卫与锁

我们看 `singleton.hpp`，它是一个 CRTP 的壳，头文件的注释把来历交代了，内核里每一个只该有一份的服务都手写过同样的五行 `self()`，十份文件是一样的模样，这个壳把那五行收成了自己的一份。用法的三步是继承它、声明友元、私有构造，`self()` 里的局部静态标了 `constinit`，注释把立场讲完了，它的立场是拒绝动态初始化，平凡析构让退场的注册消失，`static_assert` 守的是平凡析构这个条件，谁的成员需要运行期析构，编译期就过不去了。PMM、调度器、文本台的 `self()` 全是同一个模子。

我们接着看 `scoped_guard.hpp`，它是一个 RAII 的壳，管的是进门做一件事、出门收回来的纪律。政策要交出的是 `State`、`enter`、`exit` 这三样，壳本身的长度是五行，拷贝与移动都删了，注释给的理由是拷一个守卫等于双重出门。政策住在各自的家里，抢占的家在 proc，中断旗标的家在 arch，锁的家在同步站，这层安排的意图注释也写了，壳与操作系统是无关的，host 上的测试拿玩具政策就测得起来，`guard` 那场测试用的就是玩具。

`spin_mutex.hpp` 的 `SpinMutex` 是一把不自带中断政策的自旋锁，`lock` 是两层的转，外层的一转是带 acquire 的 `test_and_set`，转圈的时候退到里层 `test` 的宽松读，打拍靠的是信号栅栏，`unlock` 是一次 release 的清除。注释把边界划给了调用者，中断的开关它不管，短小、不睡眠的临界区是它的领地。`guard()` 给出的是一个作用域守卫，锁的生死跟着作用域走，控制台的输出闸就是它加上中断守卫的组合，我们在打印链那一段里见过这一对。原子的现状本页补一句，旧页里自实现的原子文件已经不在了，内核如今直接用工具链的 `<atomic>`，`SpinMutex` 的 `atomic_flag`、`RefCount` 的 `atomic<Count>`、调度器里的 `atomic<bool>` 抢占开关都是证据，两个世界的语义由工具链保证是同一份。

## 地址、数值与结果

我们从 `math.hpp` 认起，它的一组算术动词是 `Min`、`Max`、`Clamp`、`Floor`、`Ceil`、`Span`、`AlignUp`、`AlignDown`，注释里说这里的单位是参数，页、扇区或者别的粒度共用的都是这一套词，全程都是 constexpr 的算术。`addr.hpp` 的 `PhysAddr` 把物理地址立成了自己的类型，随手把它当整数算是它不许的，进来的那一道转换是显式的，零兼作没有地址的答案，注释里说分配器拿零当空的答案。`ptr.hpp` 的 `PtrAt<T>` 是整数地址变指针的唯一转接点，注释里说裸位就是它的意义，格式化的 `Arg` 把指针按位存，取回来的时候靠的也是它。`literal_types.hpp` 给了 KiB、MiB、GiB、TiB 的字面量与一个 `Hertz` 的强类型，节拍频率在接口上走的是它，光秃秃的无符号数是进不了场的。

剩下的小件我们两句一个带过。`byte_order.hpp` 管的是小端与大端的标量读写，网络栈的协议头靠它拆与装。`checksum.hpp` 的内容是 RFC 1071 的反码校验和，字节段是它可以分批收下的，跨奇数边界的部分也算得拢。`random.hpp` 的生成器注释写得坦率，种子的播撒只有一次，它是不可信的，内核的熵源拿它打底。`time.hpp` 的家当是日历时间的拆分与秒的强类型，RTC 读出来的字段进它，纪元秒的换算是纯算术。`parse.hpp` 干的是 ASCII 转数的解析，八进制与十进制的整字段解析，ustar 头部的长度字段是注释里的头一位调用者。`path.hpp` 干的是斜杠路径的视图游走，组件的行走、父与叶的拆分都有各自的入口，根路径与否的判断也有专门的入口。`result.hpp` 的本体是值与错误的判别联合，`Result<T>` 与 `Result<void>` 的声明都标着 `[[nodiscard]]`，丢弃返回值的代码会让编译器出声，错误码是 base 自己的 `KernelError`，名字表配的是 `ErrorString`，引用它的源文件在网络、文件系统与系统调用里都见得到。`async.hpp` 里有一块回调的板子，USB 设备的完成通知骑在 `AsyncCallback` 上，`region/memory_region.hpp` 的区间描述被 VMM 的布局配置引用，这两样的细节本页不展开。

## host 测试怎么直接链它们

```cmake
# test/CMakeLists.txt
function(add_cinux_test name)
    add_executable(test_${name}
        unit/test_${name}.cpp
        ${ARGN}
        framework/framework.cpp
        framework/assert_host.cpp
        framework/per_cpu_host.cpp
        framework/irq_guard_host.cpp)
    target_include_directories(test_${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(test_${name} PRIVATE cinux_base cinux_warnings)
    add_test(NAME ${name} COMMAND test_${name})
endfunction()
```

骨架摆在这里了，我们把它念开。排在头里的源文件是 `unit/test_${name}.cpp`，`${ARGN}` 的位置留给内核源文件，框架的四个文件每一场都到，断言钩子的 host 定义就藏在 `assert_host.cpp` 里，它跟内核的 `kernel/lib/assert.cpp` 是一对孪生，世界换装换的就是这个文件。头文件的搜索路径一共是三条，走的是测试框架、base 的 include 与仓库根，链接的是 base 的静态库，格式化、解析与路径的编译单元都在里面。

纯 base 的件在 host 上连一行内核源文件都不用给，（`format`、`bitmap`、`bits`、`checksum`、`result`、`path`、`text`、`random`、`time`、`directed_graph`、`static_stack`、`slot_table`、`ref_count`、`self_list`、`shared`、`guard`）这样的测试全是头文件的直链，内联的件由编译器实例化出来，断言钩子换成 host 的，跑的就是普通进程。内核的源文件想进 host 也是可以的，我们在名单里挂上路径就是了，`pmm`、`heap`、`lockdep`、`scheduler`、`sync`、`vfs` 这些测试都这么干，能成的前提是那些 .cpp 只碰 base 与测试框架而不碰寄存器。真要压并发的测试，链的是 `Threads`，引用计数与调度器的争用就是在真线程底下跑的。

消毒器的开关也挂在链接上，base 的静态库公开地带着 `cinux_host_checks` 这个接口目标（只递旗标、自己不产出库文件），开 `CINUX_HOST_ASAN` 的时候，AddressSanitizer 与 UBSan 的旗标顺着依赖铺到每一个测试，开 `CINUX_HOST_TSAN` 的时候，换的是线程消毒器，我们要是同时打开两把开关，CMake 在配置的时候就会把错报出来。内核自己的测试走的是另一条梯子，`framework_kernel.cpp` 是框架的内核孪生，测试编进内核的镜像里，开机的地方是 QEMU，那是 [内核测试基建](test.md) 的世界，用例住在 `test/kernel/` 的目录里，本页指到的方向就到这里为止。

## 源码位置

- `kernel/lib/assert.cpp` — AssertionFailed 的内核定义与 GlibcxxAssertionFailed
- `kernel/lib/mem.cpp` — libc 拼写的内存函数与 std::terminate 的落点
- `kernel/boot/print.hpp` — Print 与 Println 的参数打包
- `kernel/console/console.cpp` — PutString 的输出闸
- `kernel/kernel.cpp` — Main 里的打印站位
- `kernel/CMakeLists.txt` — 内核镜像的源文件名单与无 SSE 孤岛
- `base/CMakeLists.txt` — base 的两个目标与编译单元名单
- `base/include/cinux/format.hpp`、`base/src/format/` — 格式化引擎的公共面与编译单元
- `base/src/parse.cpp`、`base/src/path.cpp` — 解析与路径的编译单元
- `base/include/cinux/assert.hpp` — Check 与 AssertionFailed 的共享声明
- `base/include/cinux/memory.hpp`、`text.hpp` — 字节原语与有界文本
- `base/include/cinux/own.hpp`、`shared.hpp`、`ref_count.hpp` — 所有权三件套
- `base/include/cinux/container/` — self_list、static_stack、slot_table、ring_queue、name_table、directed_graph
- `base/include/cinux/bit_ops/` — bit_ops、bitmask、bitmap
- `base/include/cinux/singleton.hpp`、`scoped_guard.hpp`、`spin_mutex.hpp`、`math.hpp` — 单件、守卫、锁与算术
- `base/include/cinux/addr.hpp`、`ptr.hpp`、`literal_types.hpp`、`byte_order.hpp`、`checksum.hpp`、`random.hpp`、`time.hpp`、`parse.hpp`、`path.hpp`、`result.hpp` — 地址、字节、时间与结果
- `base/include/cinux/async.hpp`、`region/memory_region.hpp` — 回调板子与区间描述
- `test/CMakeLists.txt` — add_cinux_test 的骨架与各测试的源文件名单
- `test/framework/assert_host.cpp` — 断言钩子的 host 定义
- `test/unit/test_format.cpp`、`test_bitmap.cpp` 等 — 纯 base 直链的样例
