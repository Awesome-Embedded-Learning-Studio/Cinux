# CinuxOS Roadmap — 长弧视图

> Tier 2（里程碑级）。状态源之一，与 PLAN/`document/todo` 同步。细节见 `document/todo/<feature>/`，依赖见 `document/todo/README.md`。
> 状态：✅ 完成 / 🔄 进行中 / ⏳ 未启动 / ⛔ 阻塞。数据源 `document/todo/README.md`（已核对）。

## Phase 0 — 基础加固  ✅ 完成
CMake 架构升级 + 大文件拆分 + 代码/注释优化审查。

## Feature 域（按依赖顺序）

| F | 名称 | Milestones（状态） | 关键产出 |
|---|------|--------------------|---------|
| F1 | 内核基础设施 | M0 ✅(类型 Cinux-Base 就绪 + ErrorOr 消费迁移: FS 层批1/2a/2b✅ + syscall→errno 批4✅); M1 RingBuffer消费迁移✅(pipe+keyboard复用Cinux-Base); M2 日志✅(KernelLog+dmesg+sys_dmesg); M3 DMA ✅; M4 块设备 ✅ | ErrorOr/StringView/Span/IBlockDevice/dmesg/DMA Pool |
| F2 | 内存管理增强 | M1 VMA✅ M2 mmap✅ M3 brk✅ M4 Page Cache✅ M5 Demand Paging✅ M6 ext2 Cache✅ M7 Buddy✅ M7b Slab✅ | mmap/Page Cache/brk/分层分配器 |
| F3 | 进程与线程 | M1 信号✅ M2 clone/futex/TLS✅ M3 进程组+waitpid阻塞✅ M4 调度器✅ | POSIX 信号/线程/futex/进程组/优先级调度 |
| F4 | SMP 多核 | M1 ACPI⏳ M2 APIC⏳ M3 AP启动⏳ M4 多核调度⏳ M5 同步原语⏳ | 多核启动/Per-CPU/ticket lock |
| F5 | 设备驱动 | M1 AHCI DMA✅ M2 VirtIO⏳ M3 NVMe⏳ M4 HPET/RTC⏳ M5 xHCI⏳ M6 E1000⏳ M7 VirtIO Net⏳ | 7 驱动 |
| F6 | VFS/文件系统 | M1 VFS增强+mount⏳ M2 ProcFS⏳ M3 DevFS⏳ M4 tmpfs⏳ M5 ext4⏳ M6 ext2独立库⏳ | Dentry Cache/5 FS/mount |
| F7 | 网络协议栈 | M1 以太网⏳ M2 ARP⏳ M3 IPv4/ICMP⏳ M4 UDP⏳ M5 TCP⏳ M6 Socket⏳ | TCP/IP+Socket API |
| F8 | IPC 扩展 | M1 Pipe增强⏳ M2 FIFO⏳ M3 Unix Socket⏳ M4 共享内存⏳ M5 epoll⏳ | CV/PTY/shm/epoll |
| F9 | 安全机制 | M1 NX/SMEP/SMAP⏳ M2 ASLR⏳ M3 UID/GID⏳ M4 Stack Canary⏳ | 硬件保护/ASLR/权限 |
| F10 | 用户态运行时 | M1 libc扩展⏳ M2 ELF动态链接⏳ M3 TTY⏳ M4 CFBox+init⏳ M5 musl+glibc⏳ | 80+ syscall/ld.so/CFBox/musl |
| F11 | 启动与平台 | M1 FAT32⏳ M2 UEFI启动⏳ | BIOS+UEFI 双启动 |
| F12 | 开发者生态 | M1 GDB/KALLSYMS⏳ M2 Lua⏳ M3 TinyCC⏳ M4 编辑器+包管理⏳ | 自举开发环境 |
| F13 | GUI 分离 | M1 ABI定义⏳ M2 Adapter⏳ M3 解耦⏳ | 独立 GUI 仓库 |

## 横切里程碑(非 Feature 域,服务于所有复杂特性)
| 标识 | 名称 | 状态 |
|------|------|------|
| FO | 可观测性/调试基建 | ✅ M0-M4 完成(2026-06-18):frame pointer(`-fno-omit-frame-pointer`,对齐 `CONFIG_FRAME_POINTER`)/ KALLSYMS lookup 模块 / 防御 backtrace(栈范围检查)/ 统一 panic handler(收编 dump_registers+kpanic+fatal_halt,+backtrace+memstats)/ `dump_memory_stats`。冒烟触发 panic 验证端到端。**M5 崩溃持久化记录:推迟**——依赖持久化层/软重启,CinuxOS 当前无(QEMU `isa-debug-exit`/halt 不保留 RAM);panic 的 serial 输出(`-serial file:`)覆盖事后取证。**M6 1b 真实符号注入(nm 嵌入):follow-up**——CMake 两阶段链接重构(风险);当前 backtrace 显示裸地址,host `addr2line -e build/kernel/big/big_kernel <addr>` 降级符号化。详见 `document/notes/2026-06-18-fo-observability.md` |
| F-INFRA | 基建加固(静态检查/指针语义/CI 粘合) | 🔄 进行中(2026-06-19 起):F2/F3 后复杂度陡增前夯基,FO 的延续(FO"崩了能看清",F-INFRA"还没崩就抓到 + CI 守住")。10 批:CI 防挂死+日志(G1/G8)/freestanding 头门禁(G9/G2)/警告标志收紧(R2)/kprintf format 属性(R2b)/static_assert 布局锁(R11)/KALLSYMS 真符号注入(R4)/64 位 gdbinit+decode-trace(R9/G5)/not_null 词汇类型(R5)/clang-tidy advisory(R8)/UBSAN freestanding 桩(R1)/lockdep-Part1 持锁调度断言(R6)。来源 26-agent workflow 验证结论。R3 原子引用计数+R6-Part2 锁序图划 F4-M5。详见 PLAN「F-INFRA」段 |

## 当前焦点
**F2-M7 Buddy PMM ✅ 完成**（2026-06-18：buddy 伙伴系统替换 PMM flat bitmap——per-order bitmap free-list 非侵入式。Bug1（direct-map reserved PF，批3）+ Bug2（WSL2 nested KVM 对侵入式 free-list 写读不一致，改 bitmap 解，GOTCHA#14）均修。**fresh KVM 742/0 + 实机 GUI 冒烟**。详见 PLAN「F2-M7 Buddy PMM」段）。

**F2-M7b SLAB ✅ 完成**（2026-06-18：kmalloc 全替 Heap——小对象→Slab 通用缓存 / 大对象→buddy+direct-map 复用；删 heap.{hpp,cpp}（净 -1951）；专用缓存 Task/VMA/CachedPage（类专属 operator new/delete 自动路由）。修 page_cache 按 `ino` 键控（slab 复用暴露的陈旧命中，GOTCHA#15）。**fresh KVM 752/0 + host 48/0 + 实机 GUI 冒烟**。详见 PLAN「F2-M7b」段 + `document/notes/2026-06-18-f2-m7b-slab.md`）。F2 收官（M1-M7 + M7b）。**FO 可观测性/调试基建 ✅ 完成**（2026-06-18）：frame pointer + KALLSYMS lookup + 防御 backtrace + 统一 panic + dump_memory_stats；冒烟触发 panic 验证端到端（符号化栈结构 + 寄存器 + task + 内存概览）。M5 崩溃记录推迟（持久化层前提）、M6 1b 真实符号注入 follow-up（CMake 两阶段，裸地址+addr2line 降级）。详见上方「横切里程碑」+ `document/notes/2026-06-18-fo-observability.md`。

**F3-M1 信号系统 ✅ 完成**（2026-06-18：核心 22 POSIX 信号 + 投递 + kill/sigaction/sigprocmask/sigreturn + Custom handler（中断路径 + int $0x80 trampoline）+ PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE 集成。**fresh 783/0 + 实机 GUI 冒烟**。详见 PLAN「F3-M1 信号」段 + `document/notes/2026-06-18-f3-m1-signals.md`）。

下个焦点：F3-M2 clone + futex + TLS。

**F3-M2 线程支持 ✅ 完成（2026-06-18：clone(56) + futex(202) + TLS(fs_base) + 线程组 + cleartid。5 批 783→810/0。共享资源 refcount 指针化（sig_actions/fd_table/cwd 真共享）；clone 子进程用户栈返回（patch syscall.S 帧 user_rsp 槽，GOTCHA#18）；cleartid exit 集成 + libc wrapper。关键踩坑 GOTCHA#17-20。**真用户态线程 round-trip + 实机 GUI 冒烟 + AddressSpace refcount + futex timeout 留 follow-up**。详见 PLAN「✅ F3-M2」段 + `document/notes/2026-06-18-f3-m2-*.md`）。

**F3-M3 进程组/会话 + waitpid 阻塞 ✅ 完成**（2026-06-19，5 批 810→827/0）：pgid/sid/setpgid/setsid/killpg + 4 syscall + waitpid 阻塞（block/unblock + exit 唤醒 parent + WNOHANG）+ exit Zombie 契约。GOTCHA#21（Scheduler::current 读静态 current_）。详见 PLAN「✅ F3-M3」段。

**F3-M4 调度器接口验证与增强 ✅ 完成**（2026-06-19，5 批 827→840/0）：①SchedulingClass 策略钩子（task_tick/task_fork/task_deadline，时间片内聚到类，删 current_slice_）②优先级感知 RoundRobin（小值优先，同优先级 RR）③多调度类实际查询（pick_next_from，schedule/exit/run_first 不再绕过 classes_[]）④SIGSTOP/SIGCONT 真调度（TaskState::Stopped 状态机 + 发送时恢复）。向后兼容（827 回归全绿）。GOTCHA#22（TaskBuilder 消耗全局 tid 计数器跨测污染）。**F3 进程与线程全里程碑收官（M1-M4）**。详见 PLAN「✅ F3-M4」段 + `document/notes/2026-06-19-f3-m4-*.md`。

下个焦点：待定 —— **F4 SMP**（多核，届时 waitpid/futex/调度器的单核 lost-wakeup 假设需重审）或 F3 follow-up（SIGKILL/SIGTERM 唤醒 Blocked 可中断睡眠、waitpid 报告 stopped/WUNTRACED、真 shell job-control 端到端验证）。

## 依赖瓶颈（影响长弧排序）
F1(IBlockDevice)→阻塞所有驱动/FS 升级；F2(mmap+PageCache)→阻塞 COW/共享内存/文件映射；F3(信号)→阻塞 TTY/shell；F4(SMP)→阻塞多核调度/APIC；F5(网卡)→阻塞整个网络栈；F10(libc+TTY)→阻塞 CFBox/Lua/TinyCC。

## 基线统计
Feature 13 / Milestone ~60 / syscall 24(目标 100+) / 驱动 8(目标 15+) / FS 2(目标 7+) / CPU 单核(目标 SMP)。
