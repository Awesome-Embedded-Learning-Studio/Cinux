# 系统文档

咱们把 Cinux 的内核子系统说明页按四个区组织起来：上手手册留给第一次来的读者，内核正文咱们按机器叙事线排好了读的顺序，接口参考与变更记录咱们让它们住在各自的分区里、由生成管线维护。每条页面讲的是子系统负责什么、关键数据结构与调用链、还有设计上的取舍，顺序咱们从引导扇区一路排到了用户态。

## 上手手册

第一次来的读者从这区入手。咱们把从源码到镜像的路、本地怎么验收各写了一页。

- [构建脚本](/systems/scripts)——从源码到镜像的构建链与磁盘卷烤制
- [测试与验收](/systems/testing)——本地全套、基线与 CI

## 内核正文

咱们按机器叙事线给这一区排了读的顺序：头一个咱们看引导怎么进内核，跟着咱们走到架构层与内存管理，然后咱们看进程与调度、同步与管道、文件系统，接着咱们把设备、系统调用与图形终端也排进来，收尾咱们回到用户态两页、测试基建与内核库。

- [引导链](/systems/boot)
- [x86_64 架构层](/systems/kernel/arch)
- [物理内存管理 (PMM)](/systems/kernel/mm/pmm)
- [虚拟内存管理 (VMM)](/systems/kernel/mm/vmm)
- [地址空间](/systems/kernel/mm/address_space)
- [堆管理 (Heap)](/systems/kernel/mm/heap)
- [进程与调度器](/systems/kernel/proc/process_and_scheduler)
- [进程的复制与替换：fork、execve 与 wait](/systems/kernel/proc/fork_exec)
- [任务的身份：pid 与线程组](/systems/kernel/proc/pid)
- [Init 任务](/systems/kernel/proc/init)
- [同步原语](/systems/kernel/proc/sync)
- [管道](/systems/kernel/ipc/pipe)
- [文件与路径](/systems/kernel/fs/file_and_path)
- [VFS 与内存盘](/systems/kernel/fs/vfs_and_ramdisk)
- [ext2 文件系统](/systems/kernel/fs/ext2)
- [AHCI 与它脚下的 PCI 总线](/systems/kernel/drivers/ahci_pci)
- [Canvas 画布](/systems/kernel/drivers/canvas)
- [键盘驱动](/systems/kernel/drivers/keyboard)
- [指点设备](/systems/kernel/drivers/mouse)
- [PIT 定时器](/systems/kernel/drivers/pit)
- [串口驱动](/systems/kernel/drivers/serial)
- [视频子系统](/systems/kernel/drivers/video)
- [系统调用](/systems/kernel/syscall)
- [Canvas & Window](/systems/kernel/gui/canvas_and_window)
- [桌面与图标](/systems/kernel/gui/desktop_and_icons)
- [Terminal 终端](/systems/kernel/gui/terminal)
- [窗口管理器 & 事件系统](/systems/kernel/gui/wm_and_events)
- [用户态 C 库](/systems/user/libc)
- [Shell](/systems/user/shell)
- [内核测试基建](/systems/kernel/test)
- [内核库](/systems/kernel/lib)

## 参考索引与变更记录

咱们让接口参考与变更记录各住各的分区：接口参考咱们交给生成管线自动产出、随代码更新，变更记录咱们按时间归档，生成与归档的流程也各有各的管线。
