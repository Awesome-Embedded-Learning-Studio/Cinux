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
与种子策略作为输入，接线顺序与生命周期归它管。这需要先确认两件事再做：

1. `SeatDeviceTree` 是否已经把设备树摆放中立化（x86 调它，arm 手写）——若是，
   arm 那段手写代码可以删掉，只用中立函数；
2. ramfs 根 + 盘挂 `/disk` 是 arm 的临时形状还是长期形状。如果是临时的（等到
   盘上根文件系统齐全就改成盘挂 `/`），那构建器要能表达"根候选"而不是写死。

## 二、客人程序的装载与 exec：arm 独有一大段

`user_fs.cpp` 里同时有：读盘上的 ELF、走中立 loader、登记 VMA、种栈页、把
busybox 铺到 ram 根的 `/bin/sh`。x86 这边的对应物分散在 `kernel/init/init_thread.cpp`
与 `kernel/loader`。这部分是否该收拢，取决于它到底是"arm 的临时生存策略"
（ramfs 里铺一个 shell 供桌面启动）还是"每个平台都要做的事"。倾向前者，所以
不急着中立化，但要在文档里写清它属于平台生存策略。

## 三、同名文件里剩下三对

`entropy`、`fork_resume`、`vmm_bringup` 三个文件两边同名。前两个是架构语义
（随机源、上下文恢复方式），同名但内容必然是各自的实现，符合预期。
`vmm_bringup` 值得再查一次：如果它做的只是"把页表建到能开分页"，两边的形状
应该接近，可考虑把其中与页表形状无关的步骤提出来。

## 四、已经中立、不用动的

故障修复（`kernel/proc/fault_resolve.cpp` 一套，死亡策略归各架构异常入口）、
写时复制（`kernel/mm/cow.cpp` 一套，架构只实现 `RepairWriteFault` 等原语）、
系统调用处理（`kernel/syscall` 一套，架构只提供 ABI 折叠）、控制台
（`kernel/console` 一套，双架构共用源文件）。

## 下一步

按上面的顺序，先做一里面的第 1 件：查 `SeatDeviceTree` 是否中立，若是就把
arm 那段手写的设备节点摆放换成调用它。这是能立刻缩小重复面的一步，且风险小。
