# Cinux 操作系统开发路线图（扁平化）

> 13 个 Feature 域，按依赖顺序推进。Phase 0（基础设施加固）保持不变。
> 每个 Feature 域内按 Milestone 排序，有明确的依赖关系。

## Phase 0: 基础加固 — **已完成**

| 子阶段 | 说明 | 状态 |
|--------|------|------|
| 0-A | CMake 架构升级 + 大文件拆分 | **已完成** |
| 0-B | 代码优化审查（纯 lib 项已移至 F1-M0） | **已合并** |
| 0-C | 注释优化审查（已分散至各 Feature 域） | **已合并** |

---

## Feature 域总览

```
Phase 0 [已完成] ─── CMake + 代码优化 + 注释
    │
    ├──→ F1  内核基础设施 ─── 核心类型库 + 日志 + 块设备抽象 + DMA
    │         ↓
    ├──→ F2  内存管理增强 ─── VMA + mmap + brk + Page Cache
    │         ↓
    ├──→ F3  进程与线程 ─── 信号 + clone/futex + 进程组
    │         ↓
    ├──→ F4  SMP 多核 ─── ACPI + APIC + 多核调度 + 同步原语
    │         ↓
    ├──→ F5  设备驱动 ─── AHCI DMA + VirtIO + NVMe + xHCI + E1000 + HPET
    │         ↓
    ├──→ F6  VFS/文件系统 ─── Dentry Cache + ProcFS + DevFS + tmpfs + ext4
    │         ↓
    ├──→ F7  网络协议栈 ─── 以太网 + ARP + IPv4 + UDP + TCP + Socket API
    │         ↓
    ├──→ F8  IPC 扩展 ─── Pipe增强 + FIFO + Unix Socket + 共享内存
    │         ↓
    ├──→ F9  安全机制 ─── NX/SMEP/SMAP + ASLR + UID/GID + Stack Canary
    │         ↓
    ├──→ F10 用户态运行时 ─── musl 移植 + ELF动态链接 + TTY + PTY 真终端会话
    │         ↓
    ├──→ F11 启动与平台 ─── UEFI启动 + FAT32
    │         ↓
    ├──→ F12 开发者生态 ─── GDB/KALLSYMS + GCC 自举（砍 Lua/TinyCC）
    │         ↓
    └──→ F13 GUI 分离 ─── Cinux-GUI submodule + 用户态 host
```

## Feature 域详细索引

| F | 名称 | Milestones | 关键产出 |
|---|------|-----------|---------|
| [F1](f1-kernel-infra/) | 内核基础设施 | M0 核心类型, M1 Ring Buffer, M2 日志, M3 DMA, M4 块设备 | ErrorOr + StringView + Span + IBlockDevice + dmesg + DMA Pool |
| [F2](f2-memory/) | 内存管理增强 | M1 VMA, M2 mmap, M3 brk, M4 Page Cache, M5 Demand Paging, M6 ext2 Cache, M7 Buddy+Slab | mmap + Page Cache + brk + 分层分配器 |
| [F3](f3-process/) | 进程与线程 | M1 信号, M2 clone/futex/TLS, M3 进程组, M4 调度器 | POSIX 信号 + 线程 + futex |
| [F4](f4-smp/) | SMP 多核 | M1 ACPI, M2 APIC, M3 AP启动, M4 多核调度, M5 同步原语 | 多核启动 + Per-CPU + ticket lock |
| [F5](f5-drivers/) | 设备驱动 | M1 AHCI DMA, M2 VirtIO, M3 NVMe, M4 HPET/RTC, M5 xHCI, M6 E1000, M7 VirtIO Net | 8 驱动 |
| [F6](f6-vfs/) | VFS/文件系统 | M1 VFS增强+mount, M2 ProcFS, M3 DevFS, M4 tmpfs, M5 ext4, M6 ext2独立库 | Dentry Cache + 5 个新 FS + mount |
| [F7](f7-network/) | 网络协议栈 | M1 以太网, M2 ARP, M3 IPv4/ICMP, M4 UDP, M5 TCP, M6 Socket | 完整 TCP/IP + Socket API |
| [F8](f8-ipc/) | IPC 扩展 | M1 Pipe增强, M2 FIFO, M3 Unix Socket, M4 共享内存, M5 poll/select | CV + PTY + shm + poll(epoll 留续) |
| [F9](f9-security/) | 安全机制 | M1 NX/SMEP/SMAP, M2 ASLR, M3 UID/GID, M4 Stack Canary | 硬件级保护 + ASLR + 权限 |
| [F10](f10-userspace/) | 用户态运行时 | M1 musl 静态移植, M2 ELF动态链接, M3 TTY, M4 PTY 真终端会话 | 112 syscall + musl ld.so + busybox + gcc 自举 |
| [F11](f11-platform/) | 启动与平台 | M1 FAT32, M2 UEFI启动 | BIOS + UEFI 双启动 |
| [F12](f12-developer/) | 开发者生态 | M1 GDB/KALLSYMS, M2 GCC 自举, M3 自举闭环, M4 包管理+编辑器 | GCC 自举（砍 Lua/TinyCC） |
| [F13](f13-gui/) | GUI 分离 | F13-A 主体, F13-B host adapter, F-GUI-USERSPACE 用户态化 | Cinux-GUI submodule + 用户态 host |

## 横切质量域

| 域 | 名称 | 内容 |
|----|------|------|
| [Quality](quality/) | 基建质量与审计 | D1-D12 审计维度、`DEBT-NNN` 债务登记、每轮审计报告 |

## 关键依赖瓶颈

1. **F1 (IBlockDevice)** → 阻塞所有驱动和文件系统升级
2. **F2 (mmap + Page Cache)** → 阻塞 fork COW、共享内存、文件映射
3. **F3 (信号)** → 阻塞 TTY job control、Shell 功能
4. **F4 (SMP)** → 阻塞多核调度、APIC 中断路由
5. **F5 (网卡驱动)** → 阻塞整个网络栈
6. **F10 (libc + TTY)** → 阻塞上层 userland（Lua/TinyCC 等）

## 统计

| 指标 | v1.0.0 | 目标（v1.1+） |
|------|------|------|
| Feature 域 | 13（8 全 ✅ / 3 部分 / F11 未启动） | 13 |
| Milestone 收官 | ~50 | ~60 |
| 总 todo 文件 | ~70 | ~70 |
| 系统调用 | 112（103 完整实现） | 持续补全 |
| 驱动 | 10 | 15+ |
| 文件系统 | 5（ext2/ext4读/tmpfs/ProcFS/DevFS） | 7+ |
| CPU 支持 | SMP -smp 2 双核 | 更多核 |
| 自举 | gcc·g++ + musl 动态 | 自举闭环 |
