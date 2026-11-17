# VFS 与内存盘

## 功能概述

这一页咱们看文件系统层的地基：vfs.hpp 定下的后端声明与挂载表，加上表上住着的头一个后端 ramfs，堆字节里的可写树。这个头一个说的是历史序，ramfs 是概念落地的第一个例子，开机现役名单里咱们找不着它，运行中的临时挂载才把它挂上表。抽象要回答的问题其实很具体：路径到了内核的手里，该请的是哪套文件系统，答完交回来的又是什么，这些都是它要定下来的事。咱们顺着一条线往下读：一个普通的类怎么变成能挂载的后端，ramfs 的字节缓冲怎么一块块扩起来，都是这一线上的事。打开文件之后的描述符与路径层次归 [文件与路径那一页](file_and_path.md)，磁盘上的 ext2 归 [ext2 文件系统](ext2.md)，/proc 那棵只读的事实树自成体系，本页只在挂载表的现役名单里给它留个名字。

## 后端接入面：FileSystem 与类型擦除

一个类型怎么才算文件系统的后端，vfs.hpp 是用一个 concept 回答的，concept 是编译期检查一组用法的语言设施，缺了一样就编不过。它要求的是这个类型用普通成员方法答得出十三个名字，咱们照着声明念一遍：

- lookup 按给它的路径找节点。
- make_file 造新的文件。
- make_dir 造新的目录。
- unlink 摘掉的是一个名字。
- read 把文件的字节读出来。
- write 把字节写进文件。
- read_dir 按着给的序号报目录条目。
- stat 把元数据填进调用方递来的结构。
- retain 给节点添上一个引用。
- release 把节点的引用放掉一个。
- readable 答的是现在读下去有没有进展。
- ioctl 接的是设备问句。
- adopt_open 在打开的时刻把节点换出来，交回来的自带一个引用。

十三个名字咱们念完了。没有要手工填的函数表，也没有虚函数的间接开销，方法凑齐了，这个类型就当得起后端的名号。

写到后端手里的路径有自己的形状，咱们也看一下：不带前导斜杠，起算的位置是挂载点（后端在挂载表里登记的那段前缀），空路径指的就是挂载根本身。read_dir 的序号参数当游标用，跟打开目录的偏移一个用法，读到哪了就报第几个孩子。

咱们把 lookup 递回来的句柄认一遍，它的名字叫 FsNode，装了四个字段：opaque 指向后端自己的节点本体，type 分的是文件、目录与字符设备，ino 是后端发的 inode 号，size 报的是字节数。句柄对调用方是半透明的，元数据是不用再问的，读就是了，但 opaque 只有铸造它的那个后端有权解释。vfs.hpp 的注释补了一条生命周期上的约束，借来的句柄想活过一次 unlink，靠的是 retain 一下，把它变成有引用保着的保留句柄。

跟句柄配套的还有两份小结构。FsDirent 装的是名字、类型与 inode 号，凑成的是一个目录条目。FsStat 是 stat 问句的答案，源码注释管它叫古典 stat 十四个字段里挑出来的一半，不过实数是六样，咱们一个一个摆出来：

- ino 认的是身份。
- mode 带的是类型与权限位。
- links 数的是有多少目录条目指名道姓地指着它。
- size 报的是字节。
- rdev 给字符设备留了身份字的位置，字节节点的这一栏一律为零。
- mtime 报的是修改时间。

没有时钟的后端把 mtime 报成零，注释的立场很干脆，咱们把这句话记下：零是诚实的话，编出来的时间才是撒谎。

后端答完了话，挂载表要能把签名各不相同的后端装进同一张函数指针表里，这样的装法叫类型擦除，咱们看它是怎么干的。门面的名字叫 FsOps，装的是一张函数指针表。生成器的名字叫 EraseFs，给过了 concept 检查的类型配上十三个无捕获的转发 lambda，无捕获的 lambda 能退化成普通函数指针，后端实例的指针在调用时当第一个实参递进来，表里从来没有它们的位置。类型收回真名的 cast 只住在生成器的唯一生成位置里，每个后端都分到了一份，落点就是 `static_cast<Fs*>(self)` 的那一下，后端自己的代码从头到尾不沾这类转型。新后端接进来的时候，一行样板也免了。

可选的部分咱们也过一眼：必答的十三样之外，可选的座位（函数表里可填可不填的位置）有七个（lookup_at、make_file_at、append、truncate、frame_at、rename、chmod），后端答不了也行。生成器用的法子是 `if constexpr` 加 requires，探到了就接上，探不到的座位就让它空着。lookup_held 与 make_file_held 两个座位留着默认的答案，默认的做法是普通解析之后补一个 retain，想答得更稳的后端自己重写，ramfs 就重写了。表里还留着 try_read 与 try_write 这样的座位，它们是非阻塞读写的轮询版本，留给会等不及的后端自己补上这个方法，[管道](../ipc/pipe.md)用的就是这一手。frame_at 给内存设备的共享映射报物理页框，凡是带字节缓冲的后端一概答不了。

resolve 是挂载表把一条绝对路径交给哪个后端的裁决口，前缀盖得长的赢。它递回来的答案咱们记作 FsView，装了四个字段：ops 指向胜出后端的门面，self 指的是后端实例，rest 是挂载点之后的剩余路径，lifetime 是可选的运行时持有，租约的事下一节读。

## 挂载表：最长的覆盖前缀胜出

Vfs 这个类自己是带着表的，咱们从它看起：表行的名字叫 Slot，装的是前缀、门面的指针、后端实例、诊断用的类型名，还有可选的租约。Vfs 是做成实例类的，文件头注释给了理由：测试世界要建自己的表，内核要建自己接线的表。表的容量定在了 kMountMax（fs_config.hpp），满了八行就不收了，门的锁是 base::SpinMutex，进出的动作都配着 arch::IrqGuard 关中断，锁的取舍归 [同步原语](../proc/sync.md)那一页。

mount() 接活的头一步是查形状，咱们跟着查一遍：前缀要以斜杠开头，长度到了 256 就拒（源码是 >= 判定），合法的最长是 255，根之外的行不许带尾斜杠，同一个前缀挂上两次也是不收的。形状过了，类型擦除就在这里发生一次了，生成的函数表带着 static constexpr 的底，同类型的挂载共享的也就是它了。type_name 参数是给诊断与挂载清单看的标签，内核接线时传的是 ext2、devtmpfs、proc 这样的名字。

unmount() 干的事很轻，把表里的行拿走就走人。这函数跟它的注释都住在 vfs.hpp 里，有一句原话说清了边界，表管的就是忘掉一行，后端自己的死活从来不是表的钱。谁想接管后端的命，挂载的时候递一个 MountLease 进来。MountLease（mount_lease.hpp）是引用计数的租约，攥着后端实例跟它的析构函数，最后一个引用放手的时刻，destroy(backend) 就把后端删掉了。咱们把租约的事说清：resolve 发现胜出的行带着租约，就用 AdoptShared 把租约的持有带进 FsView，拿到视图的调用方也顺手成了一根保命的绳，test_vfs.cpp 里有备好的用例，专门替咱们看着这个行为，已解析的视图把脱离的后端保留到最后一次释放。

resolve() 的算法咱们照 vfs.cpp 读：路径必须是绝对的，路径不是斜杠开头的话就直接拒绝。咱们把表扫一遍，前缀对不上的跳过，对上的还要过组件边界这一关，前缀长过一个字符的，路径还有剩余的时候，剩余的头一个字符必须是斜杠。挂了 /tmp 的表，服务 /tmp 与 /tmp/build 是它的本分，/tmpx 不在服务的范围之内。覆盖路径的挂载点里最长的那个赢，用例的名字说的就是这件事，最长的覆盖前缀胜出。赢家的剩余路径从挂载点起算，紧跟着的那个斜杠剥掉一个，剩下的 rest 交给后端，rest 为空的时候指的就是挂载根本身。

表还有一个只读的出口叫 mounts_text，把活着的行按 Linux mounts 的行格式写进缓冲，设备名一栏写的是 none，后面跟前缀、类型名与固定的 rw 0 0。procfs 里 mounts 文件的那个条目调的就是它，咱们下一节把它的名字在挂载表上认一下，它的走读现阶段还没有专页，本页就不展开了。

## 表上的现役名单与整读阶梯

开机时的挂载接线住在文件世界里，FileWorld 的代码在 file_world.cpp，init 把三行挂载办完了，咱们跟着念：根目录 / 挂的是 ext2，磁盘卷的走读归 [ext2 文件系统](ext2.md)。/dev 挂的是 devfs，类型名写的是 devtmpfs，设备节点是它答的。/proc 挂的则是 procfs。开机的三行之外，运行中的临时挂载另有座位机制，这里的座位是 FileWorld 手里的空位，容量照 kTmpFsSeats 定的是四个，跟前面 FsOps 函数表里可填可不填的座位是两码事。临时挂载的活走 mount_tmpfs，解析出来的得是个目录，座位上 new 的是 RamFs，配的租约是 MountLease，挂稳了之后，类型名写的是 tmpfs。

三行挂载里 /proc 那一棵值得多说几句：procfs 是个自成体系的只读后端（kernel/fs/procfs/ 四个文件一千多行），答的是内核此刻知道的事实，内存的计数、进程的名册、每个进程的生命体征，挂载表的活名单也从它的 mounts 条目里露出头，条目读的时候借的是上一节那个 mounts_text 出口，接线是 procfs_init.cpp 的活，WireKernelProcSource 把事实接进了实例。它的走读不归本页，咱们只认它在表上的名字。

表挂好了，路径也解得动了，接下来这一段咱们看文件的内容怎么整个读进内存。整读说的就是一口气把整个文件读进内存的读法。vfs.cpp 备了两个函数，走的是同一个阶梯，这个阶梯的本体是 ReadFilePrefix，内核里用它的有三处：exec 在 sys_exec.cpp 里调它读程序镜像，init 在 init_thread.cpp 里调它读要拉起来的程序，loader/interpreter.cpp 里读解释器镜像的那一路（程序头指名要带的那个解释器）调的也是它。ReadWholeFile 是不带上限的那一版，内核侧咱们暂时找不到调用方，host 的单元测试在用。阶梯是固定的五步，lookup 找的是节点，stat 问的是尺寸，按尺寸（或调用方给的上限）一次分配，读回来靠的是一次 read，读回来的数量跟要的数量核对。短读是要被拒收的，不完整的程序镜像是绝不能跑起来的，这是文件头注释立下的立场。零字节的文件算成功不算失败，ReadFilePrefix 的注释特意声明，空文件也是事实的一种。exec 拿到字节之后的事，归 [进程的复制与替换](../proc/fork_exec.md)那一页。

## 名字树：后端共用的爬路骨架

沿名字爬树是 ramfs 与 ext2 都躲不开的家务事，这件活收进了 dir_tree.hpp，咱们看骨架的形状：它把爬树的路数做成一个 CRTP 基类（基类模板拿派生类型当自己的参数的一种写法），树的形状代码写一遍，各家的后端白拿。DirNode 装的是节点的结构半边：名字、类型与 inode 号，外加孩子链与兄弟链的链接。载荷的半边归后端自己挂，ramfs 挂的是字节缓冲，ext2 挂的是盘上的引用，树从不问载荷是什么。

导航的办法都住在 dir_tree.hpp 里，咱们把五个办法清点一遍：

- find 找的是亲生的孩子。
- adopt 把新造的节点收作最新的孩子，收的位置是链头，最新的排前面。
- disown 摘下的是按名字对上的孩子，摘了但不销毁。
- child_at 取的是序号对应的那个。
- 爬路径的活归 WalkPath，它拿 PathCursor 逐组件走。

爬路径的判定咱们多记几条：路径里的 . 是跳过去的，.. 的答复是 kNotSupported，上行键（往上一层目录走的那一步）是教学内核不给的。半路踩到文件的话，报的是 kNotADirectory，test_ramfs.cpp 里有现成的用例，名字说的是路径半路踩到文件的报错，看的就是最后这一下。

## ramfs：堆字节里的可写树

咱们从 RamFs 的立场读起：这话在文件头注释里写得直白，concept 的检查与挂载表都在楼上，这个类是两样都不知道的，它只是一个用普通方法答话的后端。构造函数生出来的是根目录，根目录的 inode 号是一，节点号是往后递增的，起头的是二，锁也在这时起了名字，锁的名字是 ramfs volume。

RamNode 的家当咱们清点一遍：节点的名字叫 RamNode，继承的是 DirNode<RamNode>，载荷装了五样东西，data 是 unique_ptr 扛着的字节缓冲，capacity 记的是容量，size 报的是长度，references 管的是引用计数，linked 记的是自己还在不在树上。文件头的注释把所有权交代得很清楚，每条父子边的手里都攥着一个引用，每个打开的描述符也持有一个，最后一个引用放手的时刻，节点删掉的是自己。

写路径是这一页最值得多看两眼的地方，咱们把 write_locked 的主体搬下来：

```cpp
// kernel/fs/ramfs/ramfs.cpp
    const unsigned long kNeeded = offset + count;
    if (kNeeded > kFile->capacity) {
        const unsigned long kFresh =
            base::math::Ceil(kNeeded, kRamfsGrowthAlign) * kRamfsGrowthAlign;
        auto* const kGrown = new unsigned char[kFresh]{};
        if (kFile->size > 0) {
            base::CopyBytes(kGrown, kFile->data.get(), kFile->size);
        }
        kFile->data.reset(kGrown);
        kFile->capacity = kFresh;
    } else if (offset > kFile->size) {
        base::SetBytes(kFile->data.get() + kFile->size, 0, offset - kFile->size);
    }

    base::CopyBytes(kFile->data.get() + offset, source, count);
    kFile->size = base::math::Max(kFile->size, kNeeded);
    return count;
```

咱们一段一段过：要写的字节超出了容量，对齐的是增长的粒度，向上扩出的是一块新缓冲，花括号初始化保证新缓冲落地就是全零，旧字节是整段抄过去再换指针的。4096 这个粒度是算过的，ramfs_config.hpp 的注释说，把一兆的数据一个字节一个字节写进去，逐字节的分配要付一百万次重分配，对齐到 4096 的付法只要二百五十六次。没扩容但偏移越过了旧文件尾，中间的那个洞用 SetBytes 清零，扩容的分支里洞在零初始化里已经清好了。两条路走到的是同一处，读回来的洞都是零，堆里的陈年旧字节是成不了数据的，test_ramfs.cpp 里备的用例是两条，一条看着扩容出来的洞，一条看着没扩容时的洞。

读的方向简单，偏移落在文件尾之外的话，回的就是零个字节，Min 把请求的长度钳到剩下的尺寸。摘的一面有讲究，咱们分开看：unlink 借 disown 的手把名字从树上摘下来，要是目录里还揣着孩子的话，挂回去的是节点，报的是 kDirectoryNotEmpty。摘成了就把 linked 翻成假，放掉树持有的那个引用。名字没了不等于文件没了，还开着描述符的读写照样通，test_ramfs.cpp 里备了两条用例，名字说的是被摘了名的文件，靠着还开着的描述符照样读、照样写。

stat 的答案带着内存卷的脾气，咱们一样一样对：目录报的是 040755、链接数二、尺寸零，文件报的是 0100644 与自己的字节数，mtime 是恒为零的。readable 恒是真的，内存卷的字节永远在手边。ioctl 回的是 -ENOTTY，内存卷上是没有设备可问的。adopt_open 奉还的是原节点，再添上的是一个引用，内存卷没有克隆式的打开。

写路径剩下的两个帮手是 append 与 truncate，咱们挨个过一遍：append 在锁里取的是当前文件尾，落字节的活交给 write_locked，新的游标写回 offset 是给调用方的。truncate 清零的是 size 与 capacity，data.reset() 把整块缓冲还给堆了。

锚定这一组是给描述符层备的，咱们细看：lookup_at 拿的锚是一个持有中的目录节点，rest 是相对锚继续爬的，make_file_at 造的文件直接落在锚底下，动手之前查锚的 linked，被摘出树的目录不能再收孩子。描述符层做相对路径解析时走的就是它，细节住的就是 [文件与路径那一页](file_and_path.md)。lookup_held 与 make_file_held 是可变后端的原子版，锁还攥在手里的时候把引用加上，加上了引用才放锁，解析出来的节点在调用方接手之前不可能被 unlink 抽掉底座。没重写它们的后端拿的就是生成器的默认版，退法回到前头说的普通解析补 retain。

锁的取舍咱们也读一眼：整棵树用的是一把 proc::Spinlock，操作是从进门攥到出门的，ramfs.hpp 的注释把道理讲了，教学内核的元数据操作都是短的，注释说这正是中断阻塞的快车道管的活。创建是没有安静的失败路的，内核堆的 new 在耗尽时大声终止，所以 make_file 的错误名单一共四样，形状不对、找不到、半路踩到文件、重名都写进了名单，内存不足是进不了名单的。析构时的头一件事是让锁 retire，retire 是锁的注销口，在关中断的保护下确认没人拿着这把锁，再把登记表里的身份抹掉，retire 的细讲归 [同步原语](../proc/sync.md) 那一页。destroy_tree 的递归从根往下走，孩子是被逐个收掉的，每个节点走的是一次 release_ref，边上的引用放完了，引用的最后一个说了算，轮到谁了谁就销毁。

## 源码位置

- `kernel/fs/vfs.hpp`、`kernel/fs/vfs.cpp` — FileSystem concept、FsNode/FsDirent/FsStat、FsOps 门面与 EraseFs 生成器、挂载表与 resolve、整读阶梯
- `kernel/fs/fs_config.hpp` — 名字与路径上限、挂载表容量、三种模式字
- `kernel/fs/mount_lease.hpp` — 跨卸载的后端租约
- `kernel/fs/dir_tree.hpp` — CRTP 名字树、导航与 WalkPath
- `kernel/fs/ramfs/ramfs.hpp`、`kernel/fs/ramfs/ramfs.cpp` — RamFs 后端本体与写路径
- `kernel/fs/ramfs/ramfs_config.hpp` — kRamfsGrowthAlign 增长粒度
- `kernel/fs/file_world.cpp` — 开机三行挂载与 tmpfs 座位的接线
- `test/unit/test_vfs.cpp`、`test/unit/test_ramfs.cpp` — host 侧的挂载表与 ramfs 行为用例
