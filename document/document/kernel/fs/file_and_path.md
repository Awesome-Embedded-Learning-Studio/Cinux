# 文件与路径

## 功能概述

这一页咱们把文件与路径这一层完整地走一遍：全内核唯一的那套文件系统世界怎么接线、怎么挂出运行时的 tmpfs 座位，一个打开的文件在内核里长什么样子，描述符的号码跟打开描述怎么分工，一张任务一张的文件表怎么发号与落座，一条用户敲进来的路径怎么一步步变成某个文件系统后端里的节点，还有 flock 的文件锁与后端租约怎么收场。这一层只管打开、编号与寻路的事，装载与挂载表的机械归 [VFS 与内存盘](vfs_and_ramdisk.md)，ext2 的磁盘格式与目录项归 [ext2 文件系统](ext2.md)，管道的两端与阻塞唤醒归 [管道](../ipc/pipe.md)，系统调用的进门归 [系统调用](../syscall.md)，fork 怎么把整张表带给孩子的事归 [进程的复制与替换](../proc/fork_exec.md)，咱们用到的锁与中断原语出自 [同步原语](../proc/sync.md)，用到的地方咱们只回指。

## FileWorld：唯一的文件系统世界

咱们从全内核唯一的那套世界说起。`kernel/fs/file_world.hpp` 给 `FileWorld` 的定位是全内核唯一的一套文件系统世界，它的全部家当是一张挂载表与一个 ext2 的根。它是 `Singleton<FileWorld>` 的一个派生，跟 Pmm、Tick 一样住进 constinit 的单例壳。注释还交代了一句，后端本可以是实例化的东西，测试世界要的就是这个自由，自己的 Vfs 与替身自己造，可是跑起来的内核只要一套，接线做一次就完了。

接线咱们跟着 `FileWorld::init()` 数下来，`kernel/fs/file_world.cpp` 里的顺序是这样的：块队列骑的是 AHCI，块缓存吃的是按空闲页折算出来的预算，ext2 的卷骑在缓存上，墙钟拿的是 RTC 的秒数，然后 `mount` 把卷坐上根的位置，设备登记完了，devfs 与 procfs 也分别坐进了 `/dev` 与 `/proc`。三处挂载各配了一句 `Check`，卷要是不肯挂上的话，注释的表态是不商量，那是接线或者构建的错，机器要响亮地当场停下。头文件的注释把理由给了出来，每一次块设备的请求都会在队列上睡着，所以 init() 的容身处是已经可调度的 kernel_init 任务，引导栈满足不了的是这个条件。头文件还顺手给 ramfs 记了一笔，它留在树里当 host 测试的卷与这个概念的第一个例子，生产环境的根是磁盘。

挂载的座位咱们也看一眼。`mount_tmpfs` 在挂载互斥锁的保护下找空位，全系统的座位数下来一共四个（`kTmpFsSeats`），每个座位造出自己的 `RamFs`，这个类就是内存盘的后端，上一段里头注释留下的那个 host 测试卷 ramfs 也是它，座位挂上去用的类型名叫 tmpfs，配上自己的租约，最后一起交到挂载表的手里。`umount_tmpfs` 收得回的是运行时挂上的座位，引导时挂上的座位它碰不到，交回来的答案是 `kNotFound`。挂载表自己怎么记前缀、怎么挑中最深的挂载，那是 [VFS 与内存盘](vfs_and_ramdisk.md) 的正题，这一页只取它的答案形状。

## OpenFile：打开描述

咱们开头就把分工说明白：描述符不过是号，打开描述才装着那次打开的全部。`kernel/fs/open_file.hpp` 的头注释把话放在了开头，说用户态所在的特权环三，它手里的号只是某张任务表里的座位号，光标与它走的节点都属于那次打开本身。把这些东西搬进一个带引用计数的对象，fork 出来的孩子才能跟父亲叫同一个打开（fork 的父子俩动的是同一支光标），线程组也才能把整张表共享下来而不必造假。

```cpp
    const FsOps*             ops     = nullptr;  ///< The backend's erased face.
    void*                    backend = nullptr;  ///< The backend instance.
    FsNode                   node{};             ///< Which node of that backend.
    unsigned long            offset = 0;  ///< Bytes read so far, or the directory entry ordinal.
    /// The open's own status bits (the O_RDWR/O_APPEND/O_NONBLOCK half
    /// of the open flags): what F_GETFL answers and F_SETFL re-arms.
    unsigned long            status_flags = 0;
    base::Shared<MountLease> lifetime{nullptr};  ///< Keeps a detached runtime backend alive.


    /// Final-close observer, installed atomically by readiness registries.
    std::atomic<void (*)(const OpenFile*)> on_last_close{nullptr};

    /// Drops the node reference this open has carried since birth.
    ~OpenFile() {
        if (const auto kNotify = on_last_close.load(); kNotify != nullptr) {
            kNotify(this);
        }
        if (ops != nullptr) {
            ops->release(backend, node);
        }
    }
```

咱们把这段的字段一个个认过去，它们住在 `kernel/fs/open_file.hpp`：`ops` 与 `backend` 是后端擦除后的门面与实例，`node` 记的是这次打开走的节点句柄，`offset` 既是普通文件的读写字节数，也是目录的表项序号，`status_flags` 存的是打开标志里的状态半边（读写、追加、非阻塞这几位），fcntl 的 F_GETFL 答的就是它，F_SETFL 重新武装的也只有它，访问模式的几位从来不动（`kernel/syscall/sys_fcntl.cpp` 里拿掩码扣掉了）。计数的底细咱们在 [内核库](../lib.md) 讲过，`Shared` 每拷一次就多出一个持有的人，最后一个放手的负责回收。析构做了两件事，头一件是叫 `on_last_close` 那个最终关闭的观察者，另一件才是把出生就带着的节点引用还给后端，文件锁的收尾就挂在这个钩子上，锁的清理发生在节点引用交还之前，咱们到锁的小节再看。

## FileTable：一张任务一张表

表的身份咱们从 `kernel/fs/file_table.hpp` 读起，它用描述符的号码当索引，装的是共享的打开描述。表本身住在堆上而且可以共享，任务出生时自带的是一张（`kernel/proc/task.hpp` 的 `files` 字段），带 CLONE_FILES 的克隆不再复制、直接指回造表人的表（`kernel/proc/clone.cpp`），于是全组看的是同一批号码、同一批光标。上限的名字叫 `kFileTableMax`，配的是一百零四万个号（1024 乘 1024），头注释说这就是 Linux 默认的 nr_open。存储按需长了出来，没动到的块不分配，可块里空着的号，槽位还是各自待着的。

存储的切法是分块，咱们把块的家底数一遍。`kernel/fs/file_table.cpp` 里 `kChunkSlots` 是一个掩码字的位数，一块的容量是 64 个座位，每块装的是一张槽表加一张 cloexec 位图，块指针的数组按倍数扩容。占用是槽表自己记的，每个槽都是一个 optional 的座位，查空号是顺着槽一个槽往下扫的，按位记的只有 close-on-exec 一项。编号的发放规则是全表最低的空号，零号也是照发的。0 到 2 的位子平时握着标准流，头注释把这里面的因果讲得直白：座位是任务继承来的标准流坐下的，表自己没有任何保留的设定。一张出生就空着的表，它的头一次打开拿到的就是零号。一张装好控制台流的 shell 表，发号就从三起步了。Unix 程序熟悉的形状就是这么来的，注释的原话是 arrived at honestly。

门面与发号咱们放在一起看。表的门面是一把自旋锁加中断的快照，`TablePolicy::enter` 进门的时候存下中断状态、把闸锁上，`exit` 的出门动作是放开闸、按快照恢复，形状跟 [同步原语](../proc/sync.md) 里 IrqGuard 的纪律是同一套。发号的搜索还靠一个提示位 `next_fd_` 帮忙，`vacant_locked` 的起点是 `Max(floor, next_fd_)`，搜索的下限不高于提示位的时候，提示就推到坐过的号之后，摘号的时候又把提示拉了回去，刚腾出来的低位就不会漏。

`alloc` 的流程咱们分两窗看，头一窗在锁里探空号的有无，免得表满了还白造对象。然后到锁的外面造出 `OpenFile`，节点的引用留好，状态位也写好了，最后的落座交给 `adopt_from` 来办，正式的挑号发生在锁内。头注释把责任线交代过了，查找与分配之间的 unlink 防护归调用者管，分配成功的才会留引用，表满的时候占用的引用是零个。`free` 在锁内摘掉槽位、把 `Shared` 带了出来，出了锁才轮到真正的析构，注释把顺序交代了，表锁放掉之后后端的所有权才交出去，`free` 的实现照的就是这个次序。

收尾的活咱们数出三件。`close_all` 把整表的块搬出去再逐块删，重复的调用没有害处。`close_on_exec` 给 exec 的收尾扫尾，把武装了位的号连开带摘。`clone()` 则在一把锁里把槽位与 cloexec 位一起照了下来，这就是 fork 拿到孩子表的来路。

## 描述符的领号与落座

落座的入口咱们数出来有四个，入口的语义各不相同，咱们一个一个摆清楚。`adopt` 把别的表已有的打开描述坐到本表最低的空号上，dup 走的就是它（`kernel/syscall/sys_pipe.cpp` 里的 `HandleDup` 拿 `shared` 加 `adopt`，表满的时候交出去的是 `-EMFILE`）。`adopt_from` 把落座的下限交给调用者，从 floor 起找最低的空号，顺手把新座位带不带 close-on-exec 的事也定了，fcntl 的 F_DUPFD 与 F_DUPFD_CLOEXEC 用它，`alloc` 的尾巴也用它。`adopt_at` 的脾气是指名要号，空着的号才坐得进去，落座的时候顺手把这个号上残留的 cloexec 位清掉，头注释说这就是 dup 形状的重新落座该有的样子。`duplicate_at` 干的是 dup2 的活，把目标号上原有的打开描述原子地换掉。

```cpp
base::Result<unsigned long> FileTable::adopt_from(const base::Shared<OpenFile>& file,
                                                  unsigned long floor, bool cloexec,
                                                  unsigned long limit) {
    const TableGuard kGuard(*this);
    if (!file.holds()) {
        return base::KernelError::kInvalidArgument;
    }
    const auto kFree = vacant_locked(floor, limit);
    if (!kFree.ok()) {
        return kFree.error();
    }
    const auto  kFd    = kFree.value();
    auto* const kChunk = ensure_chunk_locked(kFd);
    const auto  kLocal = kFd % kChunkSlots;
    base::safety::Check(kChunk->slots.place(kLocal, file), "free descriptor became occupied");
    if (cloexec) {
        kChunk->cloexec.set(kLocal);
    } else {
        kChunk->cloexec.clear(kLocal);
    }
    if (floor <= next_fd_) {
        next_fd_ = kFd + 1;
    }
    return kFd;
}
```

摘自 `kernel/fs/file_table.cpp` 的这段把 `adopt_from` 的全貌摆了出来。`limit` 是不包含的上限，头注释补了一句，调低的资源上限只约束新发的号，已经发出去的老号它不动，`kernel/syscall/sys_common.cpp` 里的 `FileDescriptorLimit()` 喂的就是这个值。空号变成实座的那一下有 `Check` 兜着，报错写的是 free descriptor became occupied，锁内出这样的事就是表自己烂了，咱们读到这类 `Check` 的时候，照这个思路去想就对了。

```cpp
base::Result<unsigned long> FileTable::duplicate_at(unsigned long source, unsigned long destination,
                                                    unsigned long limit) {
    base::Shared<OpenFile> replaced;
    {
        const TableGuard kGuard(*this);
        const auto       kSource = shared_locked(source);
        if (!kSource.ok()) {
            return kSource.error();
        }
        if (source == destination) {
            return destination;
        }
        if (destination >= limit || destination >= kFileTableMax) {
            return base::KernelError::kInvalidArgument;
        }
        auto* const kChunk = ensure_chunk_locked(destination);
        replaced           = detach_locked(destination);
        base::safety::Check(kChunk->slots.place(destination % kChunkSlots, kSource.value()),
                            "duplicate destination remained occupied");
    }
    return destination;
}
```

咱们看 `kernel/fs/file_table.cpp` 里 `duplicate_at` 的这段，讲究的地方全在一个锁窗里，一条一条地看过去：源号没货的时候整体就不动，源号等于目标号是合法的空操作，答出去的直接就是目标号，越界的号不收，然后旧描述摘了下来、新描述坐了上去。换下来的那个 `replaced` 出了作用域就放手，真到最后一个号都关掉的时候，打开描述才走它的析构。系统调用那侧把它折成 dup2 的脾气，`HandleDup2` 不管里面的错是什么，答出来的一律是 `-EBADF`。控制台的座位用的是 `adopt_at`，`FileWorld::seat_console_streams` 把 `/dev/console` 解析了出来，打开的动作只发生一次，一个打开描述的读写两面都开着（状态字写的是 fs_config 里的 `kFsReadWriteStatus`），然后把三个号连坐了下来。

```cpp
    kStream->status_flags = kFsReadWriteStatus;
    for (unsigned long seat = 0; seat < 3; ++seat) {
        const base::Result<unsigned long> kSeated = table.adopt_at(kStream, seat);
        if (!kSeated.ok()) {
            return kSeated.error();
        }
    }
    return {};
}
```

这段是 `kernel/fs/file_world.cpp` 里 `seat_console_streams` 的收尾。三个座位号共享的是同一个打开，光标当然也只有一支，咱们追着 shell 的孩子想一遍，孩子读的是零号，写的是一号，报的是二号，摸到的都是同一次打开。查询与摘除也有各自的门：`get` 与 `shared` 交出的都是带所有权的 `Shared`，拿着它可以安全地穿过并发，`free` 摘的是本表的号，别的表还指着的打开照活着。光标两处同动这件事有 `test/unit/test_file_table.cpp` 的一场用例作证，用例的全名是 file table: an adopted open is shared, its cursor moves for both 连成的一句，它把父亲的 offset 写到 40，再从孩子的号里读回来验证。

## 路径解析：从用户字符串到后端节点

一条路径要过的检查咱们分成三道来看。头一道检查在系统调用的门口，`kernel/syscall/sys_path.cpp` 的 `CopyUserPath` 把用户指针上的字节按页边界分段搬进来，扫到 NUL 的时候才算搬完，然后折叠的活交给 `FoldUserPath` 干，绝对路径的底座是 `/`，相对路径拿任务的工作目录当底座，底座记在 `FsContext` 的 `cwd` 里。

```cpp
    bool walk(std::string_view view) {
        PathCursor       cursor{view};
        std::string_view part;
        while (NextComponent(cursor, part)) {
            if (part == ".") {
                continue;
            }
            if (part == "..") {
                pop();
                continue;
            }
            if (!push(part)) {
                return false;
            }
        }
        return true;
    }
```

`base/src/path.cpp` 里咱们要看的这段是 `Fold::walk`，驱动它的是 `base/include/cinux/path.hpp` 里的 `PathCursor` 加 `NextComponent`，连续的斜杠也容忍。折叠的算术咱们当纯文本读，`.` 直接就丢了，`..` 弹掉的是一层，头注释点破了实现的巧处，缓冲区自己就成了组件栈，最后一个斜杠后面的字节就是弹栈时回退到的位置，弹到根再弹的话，停的地方还是根。

上限分了内外两层，用户字符串进门的额度是 4096 字节（`kUserPathMax`，注释说是 Linux 的 PATH_MAX），折叠的结果要装进 `kFsPathMax` 的 256 字节，装不下的时候答 `-ENAMETOOLONG`。256 的来历咱们也问了一句，`kernel/fs/fs_config.hpp` 的注释把理由交代了，编译器的内建 specs 要穿过四层父目录，按名字找它们的文件，未折叠的拼法超过一百字节，教学口径的 96 会把这些路径拒成 EINVAL，256 的余量就是这么来的。

工作目录的载体咱们也认一下：`FsContext`（`kernel/fs/fs_context.hpp`）身上还带着 `root` 与 `umask`，注释的交代很诚实，今天被系统调用读的只有 `cwd`，另外两样是给 chroot 与 umask 预留的写字位。chdir 把折叠后的绝对路径写回 `cwd`，getcwd 读的也是它，fork 用 `CloneFsContext` 给孩子复制了独立的一份，CLONE_FS 的线程组共享同一份，一个线程 chdir 了之后，跟着挪的是全组的工作目录。

openat 的锚定走法是另一条路，咱们也得认：路径以 `/` 开头或者 dirfd 是 `AT_FDCWD` 的时候照旧走折叠，真给了一个目录描述符的时候不再折字符串，`OpenPathAt`（`kernel/syscall/sys_open.cpp`）拿着锚定打开描述身上的节点叫后端的 `lookup_at`，FsOps 的注释说这样认的是节点身份，目录改了名、重建了都不漂移，后端答的是 `kNotSupported` 的时候，系统调用这一侧把它折成了 `-EOPNOTSUPP`，锚定的打开把自己的租约也递给了新打开，咱们到租约的小节再看。

第二道关挑的是后端。`FileWorld::resolve` 把活转手交给了挂载表，拿回来的 `FsView` 装着四样东西（`kernel/fs/vfs.hpp`）：赢家的门面 `ops`、赢家的实例 `self`、挂载点之外的剩余路径 `rest`，还有可空的租约 `lifetime`。最长前缀怎么赢的归 [VFS 与内存盘](vfs_and_ramdisk.md) 那一页讲，咱们只认准一件事，从此往后后端的名字谁都不问，问句柄就够用了。

第三道关是后端在自己的命名树上爬。命名树的结构件在 `kernel/fs/dir_tree.hpp`，`DirNode` 的身份是一个 CRTP 基座，管的是结构性的一半：名字、类型、inode 号、兄弟与孩子的链，载荷的一半留给后端，ramfs 挂的是字节缓冲，ext2 挂的是磁盘引用，头注释定的分工是树从不过问载荷是什么。链上的动作咱们过一遍：孩子住在兄弟的链上，新来的坐最前面，`find` 的比较按名字逐个来，要求的是字节级相等、长度对上、结尾收 NUL，`adopt` 干的是收养，`disown` 的摘除不销毁孩子，`child_at` 发孩子用的是序号，读目录的序号语义跟它对得上。

```cpp
template <typename Node>
[[nodiscard]] base::Result<Node*> WalkPath(Node& root, std::string_view path) {
    Node*            current = &root;
    base::PathCursor walk{path};
    std::string_view component;
    while (base::NextComponent(walk, component)) {
        if (current->type != InodeType::kDirectory) {
            return base::KernelError::kNotADirectory;
        }
        if (component == ".") {
            continue;
        }
        if (component == "..") {
            return base::KernelError::kNotSupported;
        }
        current = current->find(component);
        if (current == nullptr) {
            return base::KernelError::kNotFound;
        }
    }
    return current;
}
```

咱们把 `kernel/fs/dir_tree.hpp` 里这段念开。爬树的错误码对着场景数出三种：半路踩到文件的时候答 `kNotADirectory`，名字不存在的时候答 `kNotFound`，`..` 的答案是 `kNotSupported`。这么严的拒绝看着有点扎眼，其实系统调用的路上碰不到它，因为折叠那一关已经把 `..` 在文本层弹掉了，走下来的全是干净的绝对路径，这个拒绝是留给直接调用爬树的自己人的护栏。ramfs 与 ext2 骑的是同一个基座，头注释说爬的这一遍谁也不用重写了。

## 文件锁与租约

`kernel/fs/file_lock.hpp` 的头注释把 flock 的归属写成了三句话：“锁属于打开描述，不属于号码，也不属于任务。”咱们把这句话落到场景上：fork 来的、dup 来的两个号都指着同一个打开，它们共享的锁是同一把，同一节点的两次独立打开就得互相争。登记表 `FileLocks` 的身份是个单例，注释说这是故意的，全机器的打开都在同一张桌上争座位。槽的数目是定长的，锁的槽位是 32 把，挂钩的槽位也是 32 个，定长换来的好处是对象可以常量初始化，安安稳稳地住进单例的 constinit 存储。每一把锁记下的是四样，后端实例、inode 号、持有它的打开描述、独占与否，判同节点靠的是头两样。

```cpp
    const auto kGuard = gate_.guard();
    if (kWord == kFlockUn) {
        drop_held(file);
        return {};
    }
    const bool          kExclusive = kWord == kFlockEx;
    const unsigned long kBlocked   = held_.find_if([&](const Held& held) {
        return names_node(held, file) && held.owner != &file && (kExclusive || held.exclusive);
    });
    if (kBlocked != kFileLockMax) {
        return base::KernelError::kWouldBlock;
    }
```

摘自 `kernel/fs/file_lock.cpp` 的这段是 `attempt` 的门口。冲突的判法咱们读出来就是一行谓词，“同一个节点、别的持有者、我要独占或者人家已经独占”，命中了就答 `kWouldBlock`。共享的读者们相安无事，独占的来了才清场。同一个打开再次申请的时候，旧的锁摘了下来、新的锁发了出去，SH 升 EX、EX 降 SH 都是一次调用的事，头注释的说法是升级降级原地完成。LOCK_UN 放掉的是这个打开的所有锁。

头注释还划了一条线，`kFlockNb` 那个位到不了的就是这一层，睡着的等待是上一层的事：`kernel/syscall/sys_flock.cpp` 拿着 `attempt` 的 `kWouldBlock` 做循环，非阻塞的场合折成 `-EAGAIN`，要等的话就 `yield()` 让出去，半路来了待决信号就答 `-EINTR`。咱们在两个进程之间用 flock 掐一回架，一个进程把文件的锁占住了，另一个进程再来要独占的时候，卡住走的就是让出的循环。

锁的死路注释列全了两条，咱们对着数一遍：一条是显式的 LOCK_UN，另一条是持有它的打开描述关掉最后一个号，靠的是 `on_last_close` 那个钩子，`FileLocks` 用比较交换把自己串进了链里，之前装上的观察者保住了还会接着跑，就绪登记表那一类也在保住的行列里。就绪登记表是 ppoll、epoll 用来记哪个打开在等可读可写的登记机构，它的实现住在 `kernel/syscall/sys_epoll.cpp`，咱们这本书没给它开专页。注释也如实记下了今天的隐患，后来的观察者裸写下来的话就会盖掉整条链。这一片的用例住在 `test/unit/test_flock.cpp`，用例的跑法归 [内核测试基建](../test.md) 那一页讲。

租约是另一条收尾的线，咱们把它也算清楚。`kernel/fs/mount_lease.hpp` 的 `MountLease` 骑着引用计数，身上带的是两样东西，一样是后端的指针，另一样是销毁的函数，最后一份放手的时候就把后端删掉。它要在场的场合是运行时挂载的后端退休：挂载表里的座位摘掉了，可是打开描述、解析视图或者映射的手里还捏着它，直接删就是悬空的指针。

所以打开的各处都得捏着一份租约，咱们把递接的路径对一遍：`OpenFile` 身上有 `lifetime` 字段，`OpenOptions` 里也存了一份，`FsView` 里也备了一份，open 的路上把解析时拿到的租约原样递给新打开（`sys_open.cpp` 里那行 `.lifetime = flat.lifetime`），openat 锚定继承的是锚自己的（`.lifetime = anchor->lifetime`）。`FileWorld::mount_tmpfs` 造座位的时候把 `RamFs` 与租约一起造了出来，挂载失败的时候就当场回收，`umount_tmpfs` 非懒惰的模式看见计数还大于一，答出去的是 `kWouldBlock`，懒惰的模式摘掉座位、留下后端，关门的事交给最后一把手。文件锁守的是并发的秩序，租约守的是生命周期。

## 源码位置

- `kernel/fs/file_world.hpp`、`kernel/fs/file_world.cpp` — FileWorld 单例、init 接线、resolve 转发、控制台座位与运行时 tmpfs 座位
- `kernel/fs/open_file.hpp` — OpenFile 打开描述、状态字、租约字段与最终关闭观察者
- `kernel/fs/file_table.hpp`、`kernel/fs/file_table.cpp` — FileTable 分块存储、发号与四个落座入口、cloexec、clone 与 close_on_exec
- `kernel/fs/dir_tree.hpp` — DirNode 兄弟链与 WalkPath 爬树
- `kernel/fs/fs_config.hpp` — 名字与路径上限、挂载数上限、模式字与读写状态值
- `kernel/fs/fs_context.hpp` — FsContext 工作目录、根与 umask，fork 复制与 CLONE_FS 共享
- `kernel/fs/file_lock.hpp`、`kernel/fs/file_lock.cpp` — FileLocks 的 flock 登记、冲突判法与最终关闭链
- `kernel/fs/mount_lease.hpp` — MountLease 后端租约
- `kernel/fs/vfs.hpp` — FsView、FsOps 门面与挂载表接口（本页只取形状，机械归 VFS 页）
- `kernel/syscall/sys_open.cpp`、`kernel/syscall/sys_openat.cpp` — open 与 openat 的折叠、锚定与落座
- `kernel/syscall/sys_pipe.cpp`、`kernel/syscall/sys_fcntl.cpp`、`kernel/syscall/sys_flock.cpp` — pipe/dup/dup2 的落座、fcntl 的 dup 与标志半边、flock 的让出循环
- `kernel/syscall/sys_path.cpp`、`kernel/syscall/sys_chdir.cpp`、`kernel/syscall/sys_getcwd.cpp` — 用户路径搬运与折叠、工作目录的读写
- `base/include/cinux/path.hpp`、`base/src/path.cpp` — PathCursor、NextComponent、FoldOntoPath
- `kernel/proc/task.hpp`、`kernel/proc/fork.cpp`、`kernel/proc/clone.cpp` — 表与 FsContext 挂在任务身上、fork 复制、CLONE_FILES 与 CLONE_FS 共享
- `test/unit/test_file_table.cpp`、`test/unit/test_flock.cpp`、`test/unit/test_fs_context.cpp` — 文件表、文件锁与文件上下文的 host 用例
