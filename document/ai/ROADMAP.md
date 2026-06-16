# CinuxOS Roadmap — 长弧视图

> Tier 2（里程碑级）。状态源之一，与 PLAN/`document/todo` 同步。细节见 `document/todo/<feature>/`，依赖见 `document/todo/README.md`。
> 状态：✅ 完成 / 🔄 进行中 / ⏳ 未启动 / ⛔ 阻塞。数据源 `document/todo/README.md`（已核对）。

## Phase 0 — 基础加固  ✅ 完成
CMake 架构升级 + 大文件拆分 + 代码/注释优化审查。

## Feature 域（按依赖顺序）

| F | 名称 | Milestones（状态） | 关键产出 |
|---|------|--------------------|---------|
| F1 | 内核基础设施 | M0 ✅(类型 Cinux-Base 就绪 + ErrorOr 消费迁移: FS 层批1/2a/2b✅ + syscall→errno 批4✅); M1 RingBuffer消费迁移✅(pipe+keyboard复用Cinux-Base); M2 日志✅(KernelLog+dmesg+sys_dmesg); M3 DMA ✅; M4 块设备 ⏳ | ErrorOr/StringView/Span/IBlockDevice/dmesg/DMA Pool |
| F2 | 内存管理增强 | M1 VMA⏳ M2 mmap⏳ M3 brk⏳ M4 Page Cache⏳ M5 Demand Paging⏳ M6 ext2 Cache⏳ M7 Buddy+Slab⏳ | mmap/Page Cache/brk/分层分配器 |
| F3 | 进程与线程 | M1 信号⏳ M2 clone/futex/TLS⏳ M3 进程组⏳ M4 调度器⏳ | POSIX 信号/线程/futex |
| F4 | SMP 多核 | M1 ACPI⏳ M2 APIC⏳ M3 AP启动⏳ M4 多核调度⏳ M5 同步原语⏳ | 多核启动/Per-CPU/ticket lock |
| F5 | 设备驱动 | M1 AHCI DMA⏳ M2 VirtIO⏳ M3 NVMe⏳ M4 HPET/RTC⏳ M5 xHCI⏳ M6 E1000⏳ M7 VirtIO Net⏳ | 7 驱动 |
| F6 | VFS/文件系统 | M1 VFS增强+mount⏳ M2 ProcFS⏳ M3 DevFS⏳ M4 tmpfs⏳ M5 ext4⏳ M6 ext2独立库⏳ | Dentry Cache/5 FS/mount |
| F7 | 网络协议栈 | M1 以太网⏳ M2 ARP⏳ M3 IPv4/ICMP⏳ M4 UDP⏳ M5 TCP⏳ M6 Socket⏳ | TCP/IP+Socket API |
| F8 | IPC 扩展 | M1 Pipe增强⏳ M2 FIFO⏳ M3 Unix Socket⏳ M4 共享内存⏳ M5 epoll⏳ | CV/PTY/shm/epoll |
| F9 | 安全机制 | M1 NX/SMEP/SMAP⏳ M2 ASLR⏳ M3 UID/GID⏳ M4 Stack Canary⏳ | 硬件保护/ASLR/权限 |
| F10 | 用户态运行时 | M1 libc扩展⏳ M2 ELF动态链接⏳ M3 TTY⏳ M4 CFBox+init⏳ M5 musl+glibc⏳ | 80+ syscall/ld.so/CFBox/musl |
| F11 | 启动与平台 | M1 FAT32⏳ M2 UEFI启动⏳ | BIOS+UEFI 双启动 |
| F12 | 开发者生态 | M1 GDB/KALLSYMS⏳ M2 Lua⏳ M3 TinyCC⏳ M4 编辑器+包管理⏳ | 自举开发环境 |
| F13 | GUI 分离 | M1 ABI定义⏳ M2 Adapter⏳ M3 解耦⏳ | 独立 GUI 仓库 |

## 当前焦点
**F1-M3 ✅ 完成**（DMA 基础设施：`DmaBuffer` + `DmaPool`（复用 direct-map）+ `PrdtBuilder`（scatter-gather），`kernel/drivers/dma/`，662→694。详见 `PLAN.md`，教训 GOTCHA #7 direct-map 勿 unmap）。下一焦点待定——F1-M4 块设备（`IBlockDevice`）或 `/milestone` 拆批。

## 依赖瓶颈（影响长弧排序）
F1(IBlockDevice)→阻塞所有驱动/FS 升级；F2(mmap+PageCache)→阻塞 COW/共享内存/文件映射；F3(信号)→阻塞 TTY/shell；F4(SMP)→阻塞多核调度/APIC；F5(网卡)→阻塞整个网络栈；F10(libc+TTY)→阻塞 CFBox/Lua/TinyCC。

## 基线统计
Feature 13 / Milestone ~60 / syscall 21(目标 100+) / 驱动 8(目标 15+) / FS 2(目标 7+) / CPU 单核(目标 SMP)。
