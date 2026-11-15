# arm 与 x86 还剩哪些成对的实现

状态：审计完成（2026-11-15）。问的是"还有哪些机制是两条车道各写一份"，答案按
可疑度从高到低排。只列已经查代码确认的，没确认的不写。

## 一、文件世界：两套接线，概念相同

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

## 二、客人程序的装载与 exec：arm 独有一大段

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

原清单备份（已执行）：

1. `user_fs.cpp`：挂 ext2 到 `/`，ramfs 只留 `/proc`、`/dev` 两个挂载点；
2. 探针路径 `/disk/etc/motd` → `/etc/motd`，`/disk/bin/sh` → `/bin/sh`；
3. 根的 getdents 计数：ramfs 的四个种子文件消失，换磁盘根的条目数；
4. 桌面车道的 `cat /disk/etc/zh` → `cat /etc/zh`；
5. 壳的落点：不再拷 `/disk/bb` 到 ramfs，直接用盘上的 `/bin/sh`。

中立化层面，文件世界构建器要表达的就是"根候选"：一个端口给 ramfs 根加若干卷
挂载，另一个端口给盘根加设备与事实挂载，两种都是同一组步骤的组合。
