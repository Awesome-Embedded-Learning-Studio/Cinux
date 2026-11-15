# arm 与 x86 还剩哪些成对的实现

状态：审计完成（2026-11-15）。问的是"还有哪些机制是两条车道各写一份"，答案按
可疑度从高到低排。只列已经查代码确认的，没确认的不写。

## 一、文件世界：接线已同构，此顶收档（2026-11-15 第三至五轮）

盘根统一后重看这顶：两边重复的机械步骤其实只剩三行挂载调用，内容（端口、
ProcSource、设备树）全是端口特有的词，当初要抽"文件世界构建器表达根候选"
的想法随 ramfs 根桥一起消失了——根候选只剩盘一种，梯子两边同构：块端口
→ MountBlockVolume → 挂 /、proc、dev。差异只剩端口类型（usdhc 对 ahci）
与盘就绪时机（探测晚于 init 开头对同步可用），都是设备本身的差异，该留。
此顶收档，不再抽构建器。

原始记录：

`kernel/fs/file_world.cpp`（150 行）给 x86 装：AHCI 或 NVMe 块设备 → 块队列 →
块缓存 → ext2 → 挂在 `/`，再挂 devfs 到 `/dev`、procfs 到 `/proc`，最后
`SeatDeviceTree` 把键盘、framebuffer、pty 这些节点摆进 devfs。

`kernel/arch/armv7/user_fs.cpp`（484 行）给 arm 装：usdhc 的块窗口 → 队列 →
缓存 → ext2 → 挂在 `/disk`，同时 ramfs 挂在 `/`、procfs 挂在 `/proc`、devfs
挂在 `/dev`，fb0 与 event0 也在这里摆进 devfs。此外它还要种 ram 根文件、加载
客人 ELF、接 exec。

两份代码用的是同一批中立件（`fs::Vfs`、`ext2::Ext2`、`fs::ProcFs`、
`fs::DevFs`、`driver::BlockQueue`、`driver::BlockCache`），所以重复的是**接线
顺序与挂载策略**，不是驱动。

差异不是随意的：x86 的根就是盘，arm 的根是 ramfs、盘挂在 `/disk`。所以中立的
做法不是把两者合成一种行为，而是抽一个"文件世界构建器"：块设备来源、各挂载点
与种子策略作为输入，接线顺序与生命周期归它管。

（订正：这份笔记初稿把设备树摆放列成了第一件待查，说"x86 调 SeatDeviceTree、
arm 手写"。查了代码，`kernel/fs/device_tree.cpp` 早就是中立面，arm 的
`user_fs.cpp` 第 249 行也已经在调它。设备树不是重复面。）

已经做完的那一半：块设备到挂载的那四步（擦除端口 → 队列 → 按预算切缓存 →
ext2 → 挂载）抽成了 `kernel/fs/block_volume.{hpp,cpp}` 的
`fs::MountBlockVolume`，两边都改用它，预算公式只有一份。

还没确认的一件：ramfs 根 + 盘挂 `/disk` 是 arm 的临时形状还是长期形状。如果是
临时的（等到盘上根文件系统齐全就改成盘挂 `/`），构建器要能表达"根候选"而不是
写死。

## 二、客人程序的装载与 exec：梯子已收成一件（2026-11-15 第六轮）

第七轮续：boot 链尾从单个 stat 小程序换成真正的 sh -c——一行命令测了
shell 解析器、stat 子进程（fork+exec）、echo 内建、cat 文件读。cat 上来
就撞见 arm 折叠表缺 sendfile64（239）：busybox 的 cat/cp 优先走 sendfile
快速路径。中立 sendfile 早就在（x86 在用），补的只是 arm 号码表与折叠表
各一行。现在卡上的 /bin/sh 作为进程跑命令行，判词 "shell ok" 与 "readme!"
进了车道。arm 侧串口还剩一个拒答：svc 90 (144) msync，与 x86 同缺，是
全项目缺口不是端口缺口。

四处 `LoadElfIntoSpace` 调用点里的三处（init 线程的首壳、exec 换核、arm 的
guest 装载）各自手拼同一副梯子：LoadWorld 组装、entry facts 补齐、GNU 栈
标志、进程布局种子、解释器。现在收成 `kernel/loader/program_seat.cpp` 的
`SeatProgram` 一件，三处都改走它。留给调用者的只有真正属于自己的话：镜像
从哪来、bias 策略（bring-up 要定基址、exec 要随机滑动）、参数块。

顺带抓出中立面里藏的端口词：`loader::kPlatformName` 写死 "x86_64"，共用源
的 exec 在 arm 上一直在报错的平台名。改成 `cinux/port/platform_name.hpp`
端口缝（照 page_walk_shape 的样子），arm 报 "armv7"。还有一处真语义差：
动态镜像缺程序头表时 exec 该答 ENOEXEC，`KernelError` 加了 `kNotExecutable`
承载这个词，ErrnoFrom 映射补齐。

glibc 车站在 arm 上跑动态链接程序，正是这条梯子的活体验证：两车道三轮
全绿，宿主 127/127。

原始记录：

`user_fs.cpp` 里同时有：读盘上的 ELF、走中立 loader、登记 VMA、种栈页、把
busybox 铺到 ram 根的 `/bin/sh`。x86 这边的对应物分散在 `kernel/init/init_thread.cpp`
与 `kernel/loader`。这部分是否该收拢，取决于它到底是"arm 的临时生存策略"
（ramfs 里铺一个 shell 供桌面启动）还是"每个平台都要做的事"。倾向前者，所以
不急着中立化，但要在文档里写清它属于平台生存策略。

## 三、同名文件里剩下三对

`entropy`、`fork_resume`、`vmm_bringup` 三个文件两边同名。前两个是架构语义
（随机源、上下文恢复方式），同名但内容必然是各自的实现，符合预期。

`vmm_bringup` 查完了：映射本身两边差得远（x86 走四级表、按 2 MiB 页填
huge window，arm 建一级描述符表、按 1 MiB 段填），没有可合并的形状。可合并的
是 `KernelTables::table` 与 `take_page` 两步——它们只依赖直接映射、页管理器和
表宽，与表形无关，已提到 `kernel/mm/kernel_tables.cpp`。`take_root` 与
`release_root` 留在各端口，因为 x86 只要一张 4 KiB 的 PML4，arm 要十六个
连续 KiB 且 16 KiB 对齐。

顺带清掉一个错误实现：arm 的 `MapDeviceWindow` 原来是死代码，语义也不对（把物理
地址当虚拟地址，再塞新分配的空帧），现在改成显式返回 false 并写清理由——这块板
的设备窗口在建页表时已按段恒等映射，驱动直接访问，不需要 ioremap 门。只有 x86
的驱动（nvme、xhci、ahci、e1000）会问这个门。

## 四、已经中立、不用动的

入场随机与栈顶滑动收进 loader（2026-11-15 第十三轮）：三个 SeatProgram 调用点审计后，
梯子本身早已一体，剩的是入场装配的拼法两边各写一份——十六字节随机（init_thread 用裸
Word 连写两拍，sys_exec 的 draw_entry_random 逐字带 TryWord 回退）与栈顶滑动（同一张
0x7FF000 掩码抄两处）。收成 loader 的 DrawEntryRandom 与 DrawEntryStackTop，两个调用点
改口，本地副本删除。exec 的栈跨度策略（RLIMIT 与画面字节数）与 init 的单页策略是各自
的真话，不动。

clone 与 openat 直折中立体（2026-11-15 第十二轮）：arm 折叠表里 clone 与 openat 原本各
垫一张本地皮——clone 只肯 fork 形（线程形 ENOSYS），openat 只认 AT_FDCWD（相对座位
EBADF）——而中立体早就是全功能（线程、TLS、tid 字、vfork、dirfd 相对打开）。删皮直折。
两件善后：共享 clone.cpp 里的 ctx.rsp 是 x86 名，为此给 fork 缝加了中立件
SetForkChildUserStack，两边各写半页（arm 改 UserTrap 的 sp，x86 改 SyscallFrame 的
user_rsp）；EABI 的 clone 拼写与中立帧（x86 序）在 tls 与 child_tid 上换位，child_tid
还在用户栈上，留一张纯拼写垫片（照 mmap2 的例）换位后仍走中立 clone。desk 车道首跑
挂 Bad address 正是换位坑：glibc 的 fork 带 CHILD_SETTID，中立 handler 在 tls 槽读到
NULL 判 EFAULT。

交互判词进车道（2026-11-15 第十一轮）：-c 链尾接 exec sh，同一进程变裸交互 shell，
stdin 第三行喂 echo 命令、第四行 exit，判词 interactive run ok 与 0 退出都进 boot 车道。
ctty 认领挪进默认交接（setsid + TIOCSCTTY），-c 与交互共用一扇门。坑：CINUX_ARM_RUN_SECONDS
是 CACHE 变量，改默认值撼不动已有 build 目录里的旧缓存 30——车道 30 秒收摊，41 秒的喂食
永远缺席；把缓存值升到 55 才见真章。

裸交互 shell 起来了（2026-11-15 第十轮）：CINUX_ARM_PROBE_INTERACTIVE=1 的构建把交接
换成裸 sh（不带 -c）。第一版卡死：ash 无限自旋 getpgrp/alarm/ioctl(TIOCGPGRP)，等自己成
为前台。用临时 svc 探针（已撤）追出真相——从没人替 shell 认领控制终端。交接前加
setsid + ioctl(0, TIOCSCTTY)（getty 的门），console 的 answer_ctty 把前台 pgrp 收养，
循环即散。手喂 echo/cat/exit 全答，guest 以 0 退出。交互车道仍属手工探针，判词未进
check（-c 链仍是车道的判据）。

交互读面通了（2026-11-15 第九轮）：stdin 文件加第二行，boot 车道改成两段喂
（第一行 t=6 给 guest 的 stdin 测试，第二行 t=21 给 shell 的 read 内建），sh 的
变量展开与 echo 把 typed:interactive ok 打回串口。中途两个坑都记着：管道串 tail
会把第一行吞掉（tail 等的是管道不是文件），单段喂会在换读者时丢头几个字节（前
8 字节没了）。下一站是 sh 不带 -c、job control 起来的完整交互。

msync 收进中立面（2026-11-15 第八轮）：glibc 车站的 fb 路径每次 msync 都撞
ENOSYS，arm 与 x86 一起缺。实现取诚实的最小语义：这个内核的映射写下即一致
（设备帧是设备自己的，私有副本在 CPU 看得见的页帧里），逐页查 VMA 覆盖，
未映射答 ENOMEM，其余答零——写回承诺本就成立，没有欠的冲刷。arm 串口的
svc 拒答从此清零。

故障修复（`kernel/proc/fault_resolve.cpp` 一套，死亡策略归各架构异常入口）、
写时复制（`kernel/mm/cow.cpp` 一套，架构只实现 `RepairWriteFault` 等原语）、
系统调用处理（`kernel/syscall` 一套，架构只提供 ABI 折叠）、控制台
（`kernel/console` 一套，双架构共用源文件）。

## 下一步

按上面的顺序，先做一里面的第 1 件：查 `SeatDeviceTree` 是否中立，若是就把
arm 那段手写的设备节点摆放换成调用它。这是能立刻缩小重复面的一步，且风险小。

## 根形状的答案：ramfs 根是过渡

查了代码，两条车道的根不同不是因为取舍不同，而是因为卡片不同：

- x86 的根就是盘。`FileWorld::init` 把 ext2 挂在 `/`，`init_thread` 从盘上的
  `/sbin/init` 装 shell，`scripts/create_ext2_disk.sh` 把这张盘做成真正的根
  文件系统：`/bin` 下 busybox 加一串 applet 链接、`/sbin/init`、`/etc/inittab`、
  `/bin/vesper` 指向桌面。
- arm 的根是内核里的 ramfs（`hello`、`motd`、`readme`、`scratch` 四个种子文件
  加 `/disk` 门），盘挂在 `/disk`，壳是把盘上的 `/disk/bb` 拷进 ramfs 的
  `/bin/sh`。原因是卡片只有几个平铺文件，还没有根文件系统的布局。

所以 ramfs 根是**过渡形状**，目标与 x86 一样是盘做根。这一轮先做半步：卡片里
已经种出 `/bin/busybox` 加 applet 符号链接、`/sbin/init`、`/usr/bin` 和
`/bin/vesper`，探针加了用例证明客人侧的 ext2 驱动能把这些快符号链接解析成真
文件——`/disk/bin/sh` 打开后读到的是 ELF 魔数。

挂载翻了（2026-11-15 第三轮）。清单里五件事的实际落点：

1. `user_fs.cpp`：盘挂载前先 `unmount("/")`，ramfs 转任 `/tmp` 暂存区，
   guest ELF 的 blob 停在 `/tmp/guest.elf`（ramfs 内部名仍是 `guest.elf`，
   `/tmp` 这半是 VFS 路由的事，不进 ramfs 路径空间）；
2. 探针路径全部翻成根视角：`/etc/motd`、`/bin/sh`、`/prog`；
3. getdents 计数从"六项"改成"至少八项且含 hello"——盘根的条目会随布局生长；
4. 桌面车道不用改 `cat`，它的输入走 USB tablet，不走路径；
5. 壳不再拷贝：`/bin/sh` 是卡上的快符号链接，ext2 的走查自己展开。

清单之外翻出来两处旧路径，是清单没照到的：`armv7_diskprog.c` 的接力
`/disk/glibc` 改 `/glibc`（这处卡了两条车道：glibc 车站与桌面都从这条
接力进来），`armv7_glibc_hello.c` 的三处 `/disk/...`。diskprog.elf 的重编
命令：`arm-none-eabi-gcc -ffreestanding -nostdlib -fno-pic -fno-builtin
-O2 -march=armv7-a -marm -T <单段链接脚本,基址0x40000000> -e disk_start`。

占位根也删了（2026-11-15 第五轮）：ramfs 先占 `/` 再让位的整段逻辑移除——
`/proc` 与 `/tmp` 挂载都走前缀路由，不需要根先存在，盘就绪后直接挂 `/`。
至此 arm 的挂载梯子与 x86 完全同构：一个块端口、一个卷、根挂 `/`、
proc 挂 `/proc`、dev 挂 `/dev`。剩下的差异只有两样，都是应该保留的：
端口类型（usdhc 对 ahci）和盘就绪的时机（探测晚于 init 开头对同步可用）。
这就是文件世界构建器"根候选"的形状：两边说的已是同一句话。

翻转后的清理（2026-11-15 第四轮）：ramfs 的过渡遗产全删——四个种子文件
（hello/motd/readme/scratch，内核侧无读者，盘上有同名文件）、种子助手、
死掉的 /disk 目录门。ramfs 现在 starts empty：占住 `/` 等盘来接，长期职责
是 `/tmp` 暂存区。两条车道全绿。

原清单备份（已执行）：

1. `user_fs.cpp`：挂 ext2 到 `/`，ramfs 只留 `/proc`、`/dev` 两个挂载点；
2. 探针路径 `/disk/etc/motd` → `/etc/motd`，`/disk/bin/sh` → `/bin/sh`；
3. 根的 getdents 计数：ramfs 的四个种子文件消失，换磁盘根的条目数；
4. 桌面车道的 `cat /disk/etc/zh` → `cat /etc/zh`；
5. 壳的落点：不再拷 `/disk/bb` 到 ramfs，直接用盘上的 `/bin/sh`。

中立化层面，文件世界构建器要表达的就是"根候选"：一个端口给 ramfs 根加若干卷
挂载，另一个端口给盘根加设备与事实挂载，两种都是同一组步骤的组合。
