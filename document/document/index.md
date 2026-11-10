# 系统文档

从引导扇区到用户态：Cinux 当前实现的内核子系统机制说明。每条页面讲一个子系统负责什么、关键数据结构与调用链、以及设计上的取舍。

## 启动

- [引导链](/systems/boot)

## 架构与内核基础

- [x86_64 架构层](/systems/kernel/arch)
- [系统调用](/systems/kernel/syscall)
- [内核库](/systems/kernel/lib)
- [小内核](/systems/kernel/mini)

## 内存管理（mm）

- [地址空间](/systems/kernel/mm/address_space)
- [堆管理 (Heap)](/systems/kernel/mm/heap)
- [物理内存管理 (PMM)](/systems/kernel/mm/pmm)
- [虚拟内存管理 (VMM)](/systems/kernel/mm/vmm)

## 进程与调度（proc）

- [进程的复制与替换：fork、execve 与 wait](/systems/kernel/proc/fork_exec)
- [Init 任务](/systems/kernel/proc/init)
- [任务的身份：pid 与线程组](/systems/kernel/proc/pid)
- [进程与调度器](/systems/kernel/proc/process_and_scheduler)
- [同步原语](/systems/kernel/proc/sync)

## 文件系统（fs）

- [ext2 文件系统](/systems/kernel/fs/ext2)
- [文件描述符 & 路径解析](/systems/kernel/fs/file_and_path)
- [VFS & Ramdisk](/systems/kernel/fs/vfs_and_ramdisk)

## 驱动（drivers）

- [AHCI 与它脚下的 PCI 总线](/systems/kernel/drivers/ahci_pci)
- [Canvas 画布](/systems/kernel/drivers/canvas)
- [键盘驱动](/systems/kernel/drivers/keyboard)
- [指点设备](/systems/kernel/drivers/mouse)
- [PIT 定时器](/systems/kernel/drivers/pit)
- [串口驱动](/systems/kernel/drivers/serial)
- [视频子系统](/systems/kernel/drivers/video)

## 图形与交互（gui）

- [Canvas & Window](/systems/kernel/gui/canvas_and_window)
- [桌面与图标](/systems/kernel/gui/desktop_and_icons)
- [Terminal 终端](/systems/kernel/gui/terminal)
- [窗口管理器 & 事件系统](/systems/kernel/gui/wm_and_events)

## 进程间通信（ipc）

- [管道](/systems/kernel/ipc/pipe)

## 用户态

- [用户态 C 库](/systems/user/libc)
- [Shell](/systems/user/shell)

## 工程与测试

- [构建脚本](/systems/scripts)
- [测试与验收](/systems/testing)
- [内核测试基建](/systems/kernel/test)
