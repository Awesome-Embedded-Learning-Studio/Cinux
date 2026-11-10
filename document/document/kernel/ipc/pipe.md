# 管道

## 功能概述

这一页我们把 `kernel/fs` 里的管道完整地走一遍。Pipe 的本体是一圈有界的环形队列，读端与写端各拿一个自己的描述符，写进去的字节，读出来的时候按的仍是写入时的顺序，容量是 `kPipeBytes` 常量的 4096 字节。头文件的注释把它的定位讲得直白，说管子把自己扮成了一个后端的文件（原文是 `pretends to be a backend file`），描述符上的读和写落进 `pull` 与 `push`，走的是卷上文件同款的 FsOps 表，所以系统调用那一层根本不知道管道的存在。同步的家当是两把信号量加一本写者的名册，读端在空的时候睡，写端在满的时候睡，最后一个写者的告别让读端读到文件尾。注释还把服务的形状照实说了，一读一写是它招待惯了的场面，环与信号量容得下更多的端，名册只回答文件尾的一个问题。

旧版的这一页我们核过一遍，有几样东西在现在的树里已经不在了，得由我们照实交代。`kernel/ipc` 目录整个没有了，旧页里的 `PipeReadOps` 与 `PipeWriteOps` 两个适配类没有去处，读与写的适配如今收在 pipe.cpp 文件尾部的一张静态 kOps 表里，两端的调用共用它。旧页写的 close() 成员也不在了，回收如今走 RefCount 的引用计数，我们到回收的小节细看。旧页写的自旋等满（`spin-wait`）过期了，写满的写端如今停在信号量上睡，不占 CPU 的周期。文件表上手动设置 0 与 1 号描述符的 set() 成员，现在的 file_table.hpp 里没有它，对应的能力叫 adopt_at。旧页还写了 Terminal 窗口拿管道接 shell，我们把 gui、console 与 tty 的源码翻了一遍，没有找到这样的消费方，这一句就不写进正文了。

管子的出生靠 `pipe` 与 `pipe2` 两个系统调用，入口在 syscall 的那一侧，我们到系统调用入口的小节走一遍。ppoll 拿它的三样只读查询当就绪的依据，sendfile 把文件灌进管道的时候走它的非阻塞门，管子断开的时候还替内核补发 13 号信号 SIGPIPE。锁与信号量的用法归 [同步原语](../proc/sync.md)，任务怎么停在信号量的等待链上、又怎么被叫回来，那一页是我们的正本。环形队列与引用计数的库侧设计在 [内核库](../lib.md)。fork 怎么把父亲的文件表带给孩子，[进程的复制与替换](../proc/fork_exec.md)讲过，我们只在用到的地方回指它们。

## 数据结构与容量

管道的全部家当都长在 Pipe 的身上，我们一个一个数。`ring_` 是环的本体，类型写的是 `RingQueue<unsigned char, kPipeBytes + 1>`，环里备的是 4097 个槽位。库的设计让容量里永远留着一个空槽，满与空的状态才分得开，能装的字节就是 kPipeBytes 的 4096。`ring_gate_` 是管门的那把 Spinlock，环、读者与写者的名册都在它的门里查，写者还剩几个的判断也在门里读。`readable_` 与 `writable_` 是管等待的两把 Semaphore，构造的时候一个从 0 起数，另一个的起点是 4096，所以 readable_ 的计数就是等在读端的字节数，writable_ 的计数就是还空着的槽位数。pipe.cpp 里的 post_each 把这层意思写成了注释，搬走的字节有几个就 post 几下，等着的伙伴看到的房间与字节，都是一个个到位的。

```cpp
// kernel/fs/pipe.cpp
Pipe::Pipe() : readable_(0), writable_(static_cast<long long>(kPipeBytes)) {
    identity_ = g_pipe_identities.fetch_add(1, std::memory_order_relaxed) + 1;
}

Pipe::~Pipe() {
    readable_.retire();
    writable_.retire();
    ring_gate_.retire();
}

namespace {

/// Posts one semaphore once per byte moved, so waiting partners see the
/// space or the payload arrive one count at a time.
void post_each(proc::Semaphore& gate, unsigned long times) {
    for (unsigned long index = 0; index < times; ++index) {
        gate.post();
    }
}

}  // namespace
```

身份与引用计数我们在两个底座上认。每根管子出生的时候，构造函数从 `g_pipe_identities` 这个全局的原子计数器上领号，取的是加一之后的值，开机后的第一根管子领到 1。两个 FsNode 的标记都把 ino 写成同一个号，所以 fstat 两端拿到的 inode 号一致，一比就认出它们是同一根管子的两端。`identity` 的注释把它的用途讲明了，拿 `(dev, ino)` 配对认管子的消费者看到的是同一根，号码的走向是从开机起一路往上涨，不过这个内核的 stat 里没有 dev 字段，眼下能对上的只有 ino。

引用计数的那半，Pipe 骑的是 `RefCount`，计数的底细 [内核库](../lib.md)已经讲过，原子计数出生的时候就是 1，加一的动作走 `retain`，减一的动作走 `release`，对没有所有者的计数再加、加到上限的地方再加、没有持有的时候还放，三样都会在 Check 的关口当场停机。析构函数做的只有收尾，两把信号量与管门的那把自旋锁各调一次 `retire()`，retire 的要求我们在同步原语那一页见过，等待链上还有人的话就当场拦下。

## 读写端：一张 FsOps 表，两个节点标记

适配的活收在一张表里，我们把 pipe.cpp 尾部的静态 kOps 一个个排开。`read` 与 `write` 落进的是 pull 与 push，`try_read` 落进的是非阻塞版的 pull，`try_write` 落进的是 try_push，stat 答话的是 pipe_stat，retain 与 release 守住的是生命周期一节的那对钩子。路径一侧的 `lookup`、`make_file`、`make_dir`、`unlink`、`read_dir`、`ioctl` 都没有实现，因为管道在文件树上没有它的名字，唯一的出生方式是 pipe 系统调用，也没有被 open 找到的那一天。

两端的区分不靠两个类，我们把标记的位置读出来，它写在节点的 opaque 字段里。读端的 opaque 是空指针，写端的 opaque 是 1，retain 与 release 的钩子拿它做判断，读者来了往读者的名册加一，写者来了往写者的名册加一，放手的时候照着减。管道的 stat 回答的是 fifo 的模样，mode 是八进制的 0010600，类型位落在 fifo（字节流的设备文件类别），权限位记的是属主可读可写，links 答的是 1，size 答的是 0，pipe_stat 的注释只有一句 `a fifo, both ends one identity`，两端的身份是同一个，说的就是前面那个 ino。

游标在管道的身上是个摆设，我们照实记录。`pipe_read` 与 `pipe_write` 收下 FsOps 签名里的 offset 参数，定义里连它的名字都没起，注释写的是 FsOps 的签名要摆满五个名字，游标在这个后端身上是不读的，读写落的位置永远是环的头与尾。`sys_read` 那一侧倒是把读到的字数加进了 `OpenFile` 的 `offset`，所以游标在动、管子不看，lseek 挪的只是描述符上的一个数。一次系统调用能搬的字节也有上限，sys_read 与 sys_write 各备了一个 1024 字节的中转缓冲，注释把这层交代了，任何后端都不直接碰 ring 3 的地址。所以一次 read 至多搬回 1024 字节，多出来的留给下一次调用，调用方照返回的字节数继续。

读与写的返回值我们把形状认一认。读的返回值是拿到的字节数，缓冲里存了几个就交几个，短读是合法的答案，读不满的话也不等。写的返回值照头文件注释的说法是 `always count`（一定是调用方要的数目），一次没写完的话它就不回来，等满的门路我们到阻塞的小节看。

## 阻塞与唤醒

管道的等待全部落在两把信号量上。信号量怎么把任务停进它的等待链、又怎么跟调度器的两步接口对接，[同步原语](../proc/sync.md)那一页是我们的正本，我们这里只看管道自己的用法。停车的许可检查也一并生效，`ring_gate_` 是有作用域的守卫，每个等信号量的地方，守卫都已经出了作用域，所以管道从不会拿着自旋锁去睡。SpinLedger 管的是持锁层数的登记，拦的只是持锁停车的任务，管道这样的用法它够不着。

```cpp
// kernel/fs/pipe.cpp
base::Result<unsigned long> Pipe::pull(void* sink, unsigned long count, bool nonblock) {
    if (count == 0) {
        return 0;
    }
    auto* const kOut   = static_cast<unsigned char*>(sink);
    bool        waited = false;
    for (;;) {
        unsigned long taken = 0;
        bool          closed;
        {
            const auto kGate = ring_gate_.guard();
            while (taken < count && ring_.pop(kOut[taken])) {
                ++taken;
            }
            closed = writers_ == 0;
        }
        if (taken != 0) {
            post_each(writable_, taken);
            return taken;
        }
        if (closed) {
            if (waited) {
                readable_.post();
            }
            return 0;
        }
        if (nonblock) {
            return base::KernelError::kWouldBlock;
        }
        readable_.wait();
        waited = true;
    }
}
```

我们把 pull 的循环读一遍。每一圈进门抢的都是环锁，门里把环里的字节一个个地 pop 出来，taken 的计数跟着 pop 的次数涨，写者还剩几个也在门里看了。出了锁就是分岔的路，拿到的字节数不是零的话，就按字节数把 writable_ 一一 post 了回去（拿走几个房间就还几个），然后把记下的字节数交回去。一个字节都没有的时候看两样东西。writers_ 已经归了零，读到的就是文件尾，答的是 0，要是已经等过一轮的话，就把 readable_ 的一个计数补回去，好让同样停着的伙伴也能依次醒来看到文件尾。writers_ 还在而缓冲是空的，nonblock 为真的话就答 `kWouldBlock`，不然的话就在 readable_ 上睡下去，醒来的下一圈接着走。

唤醒不丢的道理在计数里，我们把它的门道摆一摆。信号量攒的是计数而不是事件，写端 post 的那一下不管读端睡没睡，睡着的人醒来消费一个计数，没睡的人下一次 wait 立刻就拿到，所以放锁与停车之间的空档塞不进丢失的窗口。同步原语那一页讲过的 `unit transfers, no count bounce` 说的就是这层，管道是它直接的受益者。

```cpp
// kernel/fs/pipe.cpp
base::Result<unsigned long> Pipe::push(const void* source, unsigned long count) {
    unsigned long     put = 0;
    auto const* const kIn = static_cast<const unsigned char*>(source);
    while (put < count) {
        writable_.wait();
        {
            const auto kGate = ring_gate_.guard();
            if (!ring_.push(kIn[put])) {
                return base::KernelError::kOutOfMemory;
            }
        }
        ++put;
        readable_.post();
    }
    return put;
}
```

push 的循环比读的更短，我们跟着字节走一遍。房间的领取由 `writable_.wait()` 来做，没有房间的话就睡下去，醒来的时候拿着一个房间进环锁，`ring_.push` 把字节放进环的槽里，然后出了锁把 readable_ 的计数 post 一次，读端等的就是这个字节。put 的计数加一，循环到调用方要的数目全部写完才返回。环满与信号量的计数是同一份事实的两面，writable_ 的计数加上环里的字节数永远是 4096，push 里 `ring_.push` 失败的分支按不变式走不到，走到了就报 `kOutOfMemory`，算是防御的一笔。

## 非阻塞路径与断管的通知

非阻塞的门只有一个实现，push_available 一次搬的字节只有一个，我们把它的门读一遍。

```cpp
// kernel/fs/pipe.cpp
base::KernelError Pipe::push_available(unsigned char byte) {
    {
        const auto kGate = ring_gate_.guard();
        if (readers_ == 0) {
            return base::KernelError::kBrokenPipe;
        }
    }
    if (!writable_.try_wait()) {
        return base::KernelError::kWouldBlock;
    }
    bool written = false;
    bool broken  = false;
    {
        const auto kGate = ring_gate_.guard();
        broken           = readers_ == 0;
        written          = !broken && ring_.push(byte);
    }
    if (!written) {
        writable_.post();
        return broken ? base::KernelError::kBrokenPipe : base::KernelError::kIOError;
    }
    readable_.post();
    return base::KernelError::kOk;
}
```

我们把进门的顺序读一遍。它进门就把读者的名册查一遍，没有读者的话直接答 `kBrokenPipe`。接着的一步是 `writable_.try_wait()` 试领房间，领不到的话答的是 `kWouldBlock`。房间到手了之后，还得回环锁里再查一遍读者的名单，因为从第一眼到放进字节的中间，最后一个读者可能刚好走掉了，这个房间要原样地 post 回去，broken 的话答的仍是 `kBrokenPipe`，环满的时候报的是 `kIOError`。字节放进去了就 post readable_，回答的就是成功。`try_push` 按字节循环地调它，搬出去的字节照实回答，一个都没搬出去的时候才把错误交出去。

有一条限制我们照源码记录。阻塞的 push 不查读者的名册，它的等待只挂在 `writable_` 上，而房间只有 pull 来还，最后一个读者走了之后，房间永远等不来新的主人，写端的这一次 write 就停在信号量上不再回来。系统调用的那一层也没有替它拦，ErrnoFrom 把 `kBrokenPipe` 翻成的就是 EPIPE，可阻塞的路径根本产不出这个错误码。能拿到 EPIPE 的只有非阻塞的门，sendfile 灌文件的时候走的是一样的路子，它调的是 `try_push`，拿到 `kBrokenPipe` 的时候 SendSignal 把 13 号的 SIGPIPE 发给当前任务，错误的答案再交还给调用方。用这套内核的项目就得按这个现实来写自己的程序。

非阻塞旗标的来路我们对了两条。`pipe2` 的 `kOpenNonblock` 落在两端的状态字上，建好的管道两端打一开始就是非阻塞的。想中途改的话，fcntl 的 F_SETFL 把状态字重排，`sys_fcntl.cpp` 留下的是访问模式位，其余的照新旗标替换，O_NONBLOCK 挂上了之后，`sys_read` 与 `sys_write` 的读写就改走 `try_read` 与 `try_write` 的这一对门。

就绪的问句也长在三样家当上，我们把 PollOpen 的管道分支读一读。`PollOpen` 碰到 ops 是 `PipeOps()` 的描述符，读端问的是 `has_bytes`，读端这边写者走没了就补报 POLLHUP，写端问的是 `has_room`，写端没有对应的报告。ppoll 的循环不停车，扫描与让出 CPU 是交替着做的，查到就绪、信号到了或者期限到了才回用户态。

## 生命周期与回收

管道的一生从 `seat_pipe_pair` 开始，我们把它的动作顺序走一遍。`Shared` 把新管子搂在自己的怀里，引用计数的初始值 1 就是这个持有。读端的名额用 `files->alloc` 领，OpenFile 出生的时候 `ops->retain` 被调到，`pipe_retain` 给 Pipe 的计数加一次 retain，再按节点的标记给读者的名册加一。写端的领取紧跟在后面，同一根管子的计数再加一次 retain，写者那一侧的名册加一。函数返回的时候局部的 `Shared` 析构，计数的值从 3 落回 2，此后一根管子身上的引用恰好是它的两个打开描述，读端与写端的持有一边一个。

```cpp
// kernel/fs/pipe.cpp
void pipe_retain(void* self, FsNode node) {
    auto* const kPipe = static_cast<Pipe*>(self);
    kPipe->retain();
    if (node.opaque == nullptr) {
        kPipe->add_reader();
    } else {
        kPipe->add_writer();
    }
}

void pipe_release(void* self, FsNode node) {
    auto* const kPipe = static_cast<Pipe*>(self);
    if (node.opaque != nullptr) {
        kPipe->drop_writer();
    } else {
        kPipe->drop_reader();
    }
    if (kPipe->release()) {
        delete kPipe;
    }
}
```

两处失败的路我们都走一遍。写端的名额要是领不到的话，Check 会盯着读端的名额退干净，消息写的是 `pipe read end vanished while seating the write end`，调用方拿到的答案是 -EMFILE。往用户缓冲拷那一对描述符要是没拷成的话，两端的名额是一起退的，拿到的答案是 -EFAULT。失败的 pipe 不留半个开着的描述符。

名册数的是打开描述而不是描述符编号。fork 与 dup 的时候我们把这一层看得最清楚。fork 的孩子拿的是 `files->clone()` 克隆出来的新表，表里装的还是同一批 Shared 描述，读者的名册一个都不动，父子的两个 fd 说的是同一位读者。dup 与 dup2 共享的也是同一个打开描述，dup2 在目标号上把旧的换下来。名册往下走的路只有 close 一条，`files->free` 把占着的槽位摘掉，描述的最后一个 Shared 落地的时候，OpenFile 的析构把 `ops->release` 调到，`pipe_release` 按节点的标记退掉写者或读者，管子的 release 跟在后面，计数归零的那个人负责 delete。

最后一个写者的告别我们单独念一遍。`drop_writer` 发现名册归了零，就把 readable_ 的计数 post 一次，停着的读者醒来，走完 pull 的循环看到文件尾，read 拿到的答案是 0。`test_pipe.cpp` 的第一个用例的结尾演的就是这一幕，环装满了再取干，写端告别之后读端答的是 0。跨核的版本在 `test_smp_pipe.cpp` 里，两个读者的任务分别落在两个核上，对齐之后一齐停进 pull 的 readable_ 里，主测试在另一头做了 drop_writer，两个读者都在自己的核上读到了 0。

delete 的时机我们也要看一眼。停在 pull 里的读者攥着自己的打开描述，系统调用进门时 `shared()` 取的是一份持有，它的计数压着管子，管子一直活到它醒来的那一刻，析构里那三下 retire 的收尾才轮得到跑。管道的锁在 lockdep 的身份表里也有一席，`test_smp_pipe.cpp` 的第二个用例造了 65 根管子轮回地建与拆，销毁的锁把它的座位让出来，名册的容量不被撑爆。

## 系统调用入口

入口的号码是 22 与 293，`syscall.hpp` 里 kPipe 与 kPipe2 的注释把形状写明了，`pipe(fds)` 返回的一对描述符读端在前。派发表的事归 `syscall_dispatch.cpp` 管，HandlePipe 与 HandlePipe2 落的是同一个 `seat_pipe_pair`，两个入口的差别只在旗标，pipe2 的旗标字只认 `kOpenCloexec` 与 `kOpenNonblock`，混进别的位的话答的是 -EINVAL。两个旗标的白话我们也补上，kOpenCloexec 是 x86_64 的 O_CLOEXEC，管的是 exec 成功时关不关这个描述符，kOpenNonblock 对应的是 O_NONBLOCK，管的是读写走不走非阻塞的门。

`seat_pipe_pair` 的检查顺序我们跟着走。进门查的是用户指针，空指针或者越过了 `kUserAddressLimit` 的都答 -EFAULT，那个上限的值是 0x0000800000000000，也是用户地址的天花板。管子造不出来的话答 -ENOMEM。两个 FsNode 的标记造出来，读端的 opaque 是空，写端的 opaque 是 1，ino 写的都是 `identity()`，类型记的是 kFile。读端的名额领了出来，状态字带上旗标里的非阻塞位，访问模式落成的是只读，cloexec 照旗标的样子置好，上限用的是 `FileDescriptorLimit()` 读来的进程描述符上限。写端的领取紧跟在后面，状态字里多出的是 `kOpenWriteOnly`，访问模式落成的是只写。写端领不到的话读端的名额退回去，Check 盯着的是这趟回收，拿到的答案是 -EMFILE。一对号码拷回用户的缓冲，拷不动的话两个名额都退，答的是 -EFAULT，拷成了的话答 0。

dup 与 dup2 的家也在 `sys_pipe.cpp` 里，我们把它们的号码也念了，登记的是 32 与 33。dup 的做法是 `shared` 加 `adopt`，同一个打开描述落座的地方是最低的空号。dup2 的做法是 `duplicate_at`，目标号上旧的描述被换下来，失败的时候答案一律折成 -EBADF。exec 之后的收尾也在这个圈子里，cloexec 位的清扫归 `close_on_exec`，[进程的复制与替换](../proc/fork_exec.md)的 exec 一节扫过的是同一张表。

## 源码位置

- `kernel/fs/pipe.hpp`、`kernel/fs/pipe.cpp` — Pipe、pull、push、push_available、try_push、读者写者的名册、kOps 表
- `kernel/syscall/sys_pipe.hpp`、`kernel/syscall/sys_pipe.cpp` — 22 与 293 号入口、seat_pipe_pair、dup 与 dup2
- `kernel/syscall/syscall.hpp` — 系统调用号、旗标常量与 errno 的名单
- `kernel/syscall/sys_read.cpp`、`kernel/syscall/sys_write.cpp` — 1024 字节中转缓冲、非阻塞的门选择、信号与让出的重试圈
- `kernel/syscall/sys_ppoll.cpp` — PollOpen 的管道分支与 POLLHUP
- `kernel/syscall/sys_sendfile.cpp` — try_push 灌管道与 SIGPIPE 的补发
- `kernel/syscall/sys_fcntl.cpp` — F_SETFL 对状态字的重排
- `kernel/fs/file_table.hpp`、`kernel/fs/file_table.cpp` — alloc、free、clone、duplicate_at、close_on_exec
- `kernel/fs/open_file.hpp` — OpenFile 与析构里的 release 钩子
- `kernel/proc/sync.hpp`、`kernel/proc/sync.cpp` — Semaphore、Spinlock、retire
- `base/include/cinux/container/ring_queue.hpp`、`base/include/cinux/ref_count.hpp` — 环与引用计数的库侧
- `test/kernel/test_pipe.cpp`、`test/kernel/test_smp_pipe.cpp` — 往返、容量与告别、停车唤醒、跨核文件尾、锁身份回收的用例
