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
| F4 | SMP 多核 | M1 ACPI✅ M2 APIC✅ M3 AP启动✅ M4 多核调度✅ M5 同步原语✅ | ACPI+LAPIC+IOAPIC+PIC→APIC切换 ✅；per-CPU 架构(GS/gdt_blocks/swapgs)✅；IPI+trampoline+AP boot ✅(-smp 2 双核 online+idle)；**M4 多核调度 ✅**(per-CPU idle+runq+reschedule IPI+prepare-to-wait+真 user-task 迁移;-smp 2 AP 真跑 user task/无 GP)；**M5 同步原语 ✅**(R3 原子引用计数 SharedCwd/SharedSigActions + R6-Part2 lockdep 锁序图死锁检测 opt-in CINUX_LOCKDEP)。**F4 SMP 全域 M1-M5 收官**；**+F4-followup SMP 迁移竞态修复 ✅**（task on_cpu 关闭迁移时旧/新 CPU 并发存取同一 ctx 的竞态,对齐 Linux task_struct->on_cpu;feat/smp-migration-fix 68b1913 待 PR;F-GUI-DECOUPLE 批2 触发定位） |
| F5 | 设备驱动 | M1 AHCI DMA✅ M2 VirtIO⏳ M3 NVMe⏳ M4 HPET/RTC⏳ M5 xHCI✅ M6 E1000✅ M7 VirtIO Net⏳ | 7 驱动 |
| F6 | VFS/文件系统 | M1 VFS增强+mount⏳ M2 ProcFS⏳ M3 DevFS⏳ M4 tmpfs⏳ M5 ext4⏳ M6 ext2独立库⏳ | Dentry Cache/5 FS/mount |
| F7 | 网络协议栈 | M1 以太网✅ M2 ARP✅ M3 IPv4/ICMP✅（2026-06-26 真 ping 10.0.2.2 干通,底子优先 L0-L3:netdev 抽象+loopback 确定性打底→e1000 收尾） M4 UDP⏳ M5 TCP⏳ M6 Socket⏳ | TCP/IP+Socket API |
| F8 | IPC 扩展 | M1 Pipe增强⏳ M2 FIFO⏳ M3 Unix Socket⏳ M4 共享内存⏳ M5 epoll⏳ | CV/PTY/shm/epoll |
| F9 | 安全机制 | M1 NX/SMEP/SMAP✅(2026-06-25;NXE+SMEP+SMAP 均真生效并验证:修 3 bug—CPUID.07H 子叶 ecx=0 / test harness 调 enable_smep_smap / test 读用户 stac;-cpu host 透传 932/0+test_f9 验证 CR4 设;默认 -cpu max WSL2 KVM 隐藏 leaf 7 故 CPUID-gated 跳过) M2 ASLR✅ M3 UID/GID✅ M4 Stack Canary✅（批6 -fstack-protector-strong + TSC canary + __stack_chk_fail kpanic，932/0）| 硬件保护/ASLR/权限;详见 PLAN「🔄 F9」 |
| F10 | 用户态运行时 | **M1 musl 静态移植🔄（2026-06-26 立项，砍自建 libc，直接移植 musl）** M2 ELF动态链接⏳ M3 TTY🔄（2026-06-27 立项 Phase 1） M4 CFBox+init⏳ | musl libc/sysroot/musl-gcc/CFBox；详见 PLAN「🔄 F10-M1」 |
| F11 | 启动与平台 | M1 FAT32⏳ M2 UEFI启动⏳ | BIOS+UEFI 双启动 |
| F12 | 开发者生态 | M1 GDB/KALLSYMS⏳ M2 Lua⏳ M3 TinyCC⏳ M4 编辑器+包管理⏳ | 自举开发环境 |
| F13 | GUI 分离 → **visor 跨平台库** | 立项调研✅(DRAFT 2026-06-21):visor 七层架构 + profile ceiling;M0-M9 待确认启动 | 独立 visor 仓库(submodule)+ Cinux host adapter;详见 `document/todo/f13-gui/` + `document/notes/2026-06-21-f13-visor-*.md` |

## 横切里程碑(非 Feature 域,服务于所有复杂特性)
| 标识 | 名称 | 状态 |
|------|------|------|
| FO | 可观测性/调试基建 | ✅ M0-M4 完成(2026-06-18):frame pointer(`-fno-omit-frame-pointer`,对齐 `CONFIG_FRAME_POINTER`)/ KALLSYMS lookup 模块 / 防御 backtrace(栈范围检查)/ 统一 panic handler(收编 dump_registers+kpanic+fatal_halt,+backtrace+memstats)/ `dump_memory_stats`。冒烟触发 panic 验证端到端。**M5 崩溃持久化记录:推迟**——依赖持久化层/软重启,CinuxOS 当前无(QEMU `isa-debug-exit`/halt 不保留 RAM);panic 的 serial 输出(`-serial file:`)覆盖事后取证。**M6 1b 真实符号注入(nm 嵌入):follow-up**——CMake 两阶段链接重构(风险);当前 backtrace 显示裸地址,host `addr2line -e build/kernel/big/big_kernel <addr>` 降级符号化。详见 `document/notes/2026-06-18-fo-observability.md` |
| F-INFRA | 基建加固(静态检查/指针语义/CI 粘合) | ✅ 完成(2026-06-19,10 批):CI 防挂死+串口日志(G1/G8)/freestanding 头门禁+修 <array>+GCC 断言(G9/G2)/警告标志收紧至零警告(R2)/kprintf format 属性+修 21 处不匹配(R2b)/static_assert 锁 5 结构体布局(R11)/KALLSYMS nm 真符号注入(R4)/64 位 gdbinit+decode-trace demangle(R9/G5)/NotNull<T> 指针契约+scheduler 采纳(R5)/clang-tidy 精选 advisory(R8)/UBSAN freestanding 桩 GCC void* 签名(R1)/lockdep-Part1 持锁跨 schedule 检测(R6)。全程基线 840/0 绿+零警告;UBSAN 构建 840/0 零 UB 命中(F2/F3 UB-clean)。R3 原子引用计数+R6-Part2 锁序图划 F4-M5。详见 PLAN「✅ F-INFRA」段 + 各批 notes |
| F-QA | 质量收敛与加固(deterministic 审计/门禁/类型不变量/修债) | ✅ 收官(2026-06-21):Q1✅合 main(PR#25 零成本门禁+CI 矩阵) / Q2+Q3+DEBT-017✅合 main(PR#26,14/14 审计+deterministic 方法论+host-ASAN 门禁) / Q4a✅(RefCount+UserPtr 类型) / **Q4b-e✅**(DEBT-001/002/003/004/005/006 全修,feat/f-qa-q4 待 PR)。基线 875→887/0;-smp2+LOCKDEP+host-ASAN 全绿。详见 PLAN「✅ F-QA」段 + notes |
| F-CLN | 债务清理（xHCI 审补全 + 修 open 债） | ✅ 收官(2026-06-25,feat/f-cln-debt):批0 xHCI/USB 专项审→DEBT-021(P1 登记,留 xHCI 重构) + 批1-7 修 DEBT-015/016/018/008/009/010/007 全✅ + 批8 收尾。931/0 + -smp2 + LOCKDEP + ctest54/0 全绿。DEBT-019/013/020/012 留 F10 顺手。详见 PLAN「✅ F-CLN」段 |
| F-VERIFY | 动态验证与并发检测基建（测试保真度/SMP 覆盖/并发检测/机制回读/故障可观测） | 🔄 **核心完成 待 PR**（feat/f-verify,18 commit,2026-06-27）：起源 audit（165 调试时间坑）——14 维静态债审量错轴（代码对≠被跑到/机制真生效/崩了看得懂）。**核心 landed**：M0 零风险赢点（RUN_ALL_TESTS 虚报修/OSFXSR 回读/0x%p 清扫/check_memory_layout 进 CI，砍 3 条 audit 误判）/ M1 测试矩阵 47×6 坐实（37/47 真码没链、47/47 SMP 空转量化）/ 统一入口 run-kernel-test-all（单核→-smp 一条命令）/ **M3 破解 47/47 SMP 空转**（AP 真醒+CR4/EFER/LSTAR 回读）/ M4-TSAN 并发检测（CINUX_HOST_TSAN+CI，60/60 零 race）/ **M5 真 fork/CoW 压力**（M5-1 真标记测试消头号假测 + M5-2a/2b -smp 真跨核 forktest races=0，F10 四修稳）/ M6-1/2 故障可观测（#PF debugcon 首故障+CoW mapcount 解码）。验证 run-kernel-test-all 955/0 + test_host 60/60。**Follow-up**：M4 KCSAN-lite（F-QA Q4 已锁已知 race，价值降）、M2 消镜像副本链真码、M6-余（用户栈 dump/page-owner）。详见 note `2026-06-27-f-verify-finale` + PLAN「🔄 F-VERIFY」段 + audit memory `debugging-audit-dynamic-coverage-gap` |

## 当前焦点
**F-VERIFY 动态验证与并发检测基建 🔄 核心完成 待 PR（feat/f-verify，18 commit，2026-06-27）**：横切里程碑（与 FO/F-INFRA/F-QA/F-CLN 同档）。起源 audit（165 调试时间坑）——14 维静态债审量错轴。**核心 landed**：M0 零风险赢点 + M1 测试矩阵 47×6 坐实 + 统一入口 run-kernel-test-all + **M3 破解 47/47 SMP 空转**（AP 真醒+回读）+ M4-TSAN 并发检测 + **M5 真 fork/CoW 压力**（-smp 真跨核 forktest races=0，F10 四修稳）+ M6-1/2 故障可观测（#PF 首故障+CoW mapcount）。验证 run-kernel-test-all 955/0 + test_host 60/60。**Follow-up**（留 PR 后）：M4 KCSAN-lite、M2 消镜像副本、M6-余。push/PR 归用户。详见 note `2026-06-27-f-verify-finale` + PLAN「🔄 F-VERIFY」段。

**F10-M1 用户态运行时 / musl 静态移植 🔄 立项（2026-06-26，feat/f10-musl）**：**不自建 libc 生态**——直接移植 musl 作唯一 libc（先自己源码编译 musl + sysroot，跑通后切 musl-gcc）。砍旧 M1「自建 libc 扩 80 syscall」。调研实证：返回约定已基本 Linux 风格（-errno）；**execve 未压 auxv**（musl 头号拦路虎，批3 补）；`SYS_chdir=12` 撞 `SYS_brk=12`（批1 修，全表对齐 Linux 号）；musl 走 openat/newfstatat 不走 open/stat。6 批（0 立项 docs / 1 号纠偏+返回约定 / 2 Linux 结构体 / 3 auxv / 4 补 musl 所需 syscall / 5 musl 源码编译+sysroot / 6 端到端跑通）。详见 PLAN「🔄 F10-M1」段。

**F-CLN 债务清理 ✅ 收官（2026-06-25，feat/f-cln-debt）**：开 F10/F7 大弧前收敛 [debt.md](todo/quality/debt.md) open 债 + xHCI/USB 审计补全（Q3 后未审面）。批0 xHCI 审→DEBT-021(P1 登记,留 xHCI 重构) + 批1-7 修 DEBT-015/016/018/008/009/010/007 全✅。931/0 + -smp2 + LOCKDEP + ctest54/0 全绿。详见 PLAN「✅ F-CLN」段。

**F2-M7 Buddy PMM ✅ 完成**（2026-06-18：buddy 伙伴系统替换 PMM flat bitmap——per-order bitmap free-list 非侵入式。Bug1（direct-map reserved PF，批3）+ Bug2（WSL2 nested KVM 对侵入式 free-list 写读不一致，改 bitmap 解，GOTCHA#14）均修。**fresh KVM 742/0 + 实机 GUI 冒烟**。详见 PLAN「F2-M7 Buddy PMM」段）。

**F2-M7b SLAB ✅ 完成**（2026-06-18：kmalloc 全替 Heap——小对象→Slab 通用缓存 / 大对象→buddy+direct-map 复用；删 heap.{hpp,cpp}（净 -1951）；专用缓存 Task/VMA/CachedPage（类专属 operator new/delete 自动路由）。修 page_cache 按 `ino` 键控（slab 复用暴露的陈旧命中，GOTCHA#15）。**fresh KVM 752/0 + host 48/0 + 实机 GUI 冒烟**。详见 PLAN「F2-M7b」段 + `document/notes/2026-06-18-f2-m7b-slab.md`）。F2 收官（M1-M7 + M7b）。**FO 可观测性/调试基建 ✅ 完成**（2026-06-18）：frame pointer + KALLSYMS lookup + 防御 backtrace + 统一 panic + dump_memory_stats；冒烟触发 panic 验证端到端（符号化栈结构 + 寄存器 + task + 内存概览）。M5 崩溃记录推迟（持久化层前提）、M6 1b 真实符号注入 follow-up（CMake 两阶段，裸地址+addr2line 降级）。详见上方「横切里程碑」+ `document/notes/2026-06-18-fo-observability.md`。

**F3-M1 信号系统 ✅ 完成**（2026-06-18：核心 22 POSIX 信号 + 投递 + kill/sigaction/sigprocmask/sigreturn + Custom handler（中断路径 + int $0x80 trampoline）+ PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE 集成。**fresh 783/0 + 实机 GUI 冒烟**。详见 PLAN「F3-M1 信号」段 + `document/notes/2026-06-18-f3-m1-signals.md`）。

下个焦点：F3-M2 clone + futex + TLS。

**F3-M2 线程支持 ✅ 完成（2026-06-18：clone(56) + futex(202) + TLS(fs_base) + 线程组 + cleartid。5 批 783→810/0。共享资源 refcount 指针化（sig_actions/fd_table/cwd 真共享）；clone 子进程用户栈返回（patch syscall.S 帧 user_rsp 槽，GOTCHA#18）；cleartid exit 集成 + libc wrapper。关键踩坑 GOTCHA#17-20。**真用户态线程 round-trip + 实机 GUI 冒烟 + AddressSpace refcount + futex timeout 留 follow-up**。详见 PLAN「✅ F3-M2」段 + `document/notes/2026-06-18-f3-m2-*.md`）。

**F3-M3 进程组/会话 + waitpid 阻塞 ✅ 完成**（2026-06-19，5 批 810→827/0）：pgid/sid/setpgid/setsid/killpg + 4 syscall + waitpid 阻塞（block/unblock + exit 唤醒 parent + WNOHANG）+ exit Zombie 契约。GOTCHA#21（Scheduler::current 读静态 current_）。详见 PLAN「✅ F3-M3」段。

**F3-M4 调度器接口验证与增强 ✅ 完成**（2026-06-19，5 批 827→840/0）：①SchedulingClass 策略钩子（task_tick/task_fork/task_deadline，时间片内聚到类，删 current_slice_）②优先级感知 RoundRobin（小值优先，同优先级 RR）③多调度类实际查询（pick_next_from，schedule/exit/run_first 不再绕过 classes_[]）④SIGSTOP/SIGCONT 真调度（TaskState::Stopped 状态机 + 发送时恢复）。向后兼容（827 回归全绿）。GOTCHA#22（TaskBuilder 消耗全局 tid 计数器跨测污染）。**F3 进程与线程全里程碑收官（M1-M4）**。详见 PLAN「✅ F3-M4」段 + `document/notes/2026-06-19-f3-m4-*.md`。

**F4-M1 ACPI 静态表解析 ✅ 完成**（2026-06-19，4 批，840→859/0 + GUI 冒烟）：RSDP → find_table（RSDT 主路径）→ MADT→ACPIInfo → main 接线 + 启动探针。GOTCHA：QEMU pc 默认 ACPI 1.0（仅 RSDT，无 XSDT）/ log ANSI escape 用 `grep -a` / ACPI 表 direct-map vs APIC MMIO 需 PCD。真机 `[ACPI] 1 CPU, LAPIC 0xFEE00000, IOAPIC 0xFEC00000, 5 IRQ override, pcat=1`。详见 PLAN「✅ F4-M1」段。

**F4-M2 LAPIC + IOAPIC + PIC→APIC 切换 ✅ 完成**（2026-06-19，4 批，859→869/0 + 真机）：LAPIC（xAPIC MMIO）+ IOAPIC（indirect + set_redirect）+ IrqBackend::eoi 抽象 + switch_to_apic（mask PIC / LAPIC enable / IOAPIC redirect IRQ0,1,12 照 ISA override）。GOTCHA：APIC MMIO 需 FLAG_PCD（非 direct-map）/ IOAPIC redirect 照 override 查 GSI（IRQ0→GSI2）/ qemu64 无 x2APIC。真机 `[APIC] switched` + **PIT 13 ticks under APIC**（路由通）。详见 PLAN「✅ F4-M2」段。

**F4-M3 Phase 1 per-CPU 架构 ✅ 完成**（2026-06-19，P1-1~4，869→869/0 全程行为不变 + 真机 GUI）：P1-1 PerCpu 块+percpu() 替 g_per_cpu（eaccc57）；P1-2 GS base 锚定 PerCpu[0]+完整 swapgs 纪律（c1a511e，**原设计低估 swapgs 牵连——ISR 无 swapgs，补条件 swapgs**）；P1-3 per-CPU GDT/TSS gdt_blocks[]（b9af79f）；P1-4 收尾。内核态 percpu() 读 MSR_GS_BASE、每 CPU 独立 GDT/TSS、syscall/ISR/jump_to_usermode swapgs 纪律全就绪。详见 PLAN「✅ F4-M3」段 + `document/notes/2026-06-19-f4-m3-phase1-recap.md`。

**F4-M3 Phase 2 AP 启动 ✅ 完成**（2026-06-19，P2-1~5，872/0 + 真机 -smp 2 双核）：P2-1 LAPIC IPI（ICR + send_init/sipi/ipi，PR#22 b643ebc）；P2-2/3/4b trampoline + ap_main + BSP INIT-SIPI-SIPI（1194345）。**F4 最难的 trampoline 首次实跑即通**：AP 经 16→32→64 @0x8000 到 ap_main，设 GS/GDT/IDT/LAPIC，online + halt；BSP 续跑到 GUI。**关键 GOTCHA**:GAS 宏不内联展开 → 用内联表达式 `(label-start+0x8000)`;CR3 切换须在 higher-half ap_entry_long(0x8000 切后不映);AP cli;hlt 不碰共享调度器(M4)。AP idle 不跑任务(留 M4)。详见 `document/notes/2026-06-19-f4-m3-p2-ap-boot.md`。**F4-M3 全里程碑收官**。

下个焦点：**F4 收官后** F4-M5 同步原语（原子引用计数 R3 + 锁序图 R6-Part2，F-INFRA 划归；waitpid 彻底 SMP 安全 children 锁）/ per-CPU APIC timer 时间片抢占（用户已定不做，follow-up）/ per-CPU runq + work stealing（性能优化，M4 用全局共享 runq 已正确）；或转 F5 设备驱动 / F10 libc+TTY。F4-M4 多核调度已 ✅（-smp 2 AP 真跑 user task，迁移 GP 根治 GOTCHA#25/26）。

## 依赖瓶颈（影响长弧排序）
F1(IBlockDevice)→阻塞所有驱动/FS 升级；F2(mmap+PageCache)→阻塞 COW/共享内存/文件映射；F3(信号)→阻塞 TTY/shell；F4(SMP)→阻塞多核调度/APIC；F5(网卡)→阻塞整个网络栈；F10(libc+TTY)→阻塞 CFBox/Lua/TinyCC。

## 基线统计
Feature 13 / Milestone ~60 / syscall 24(目标 100+) / 驱动 8(目标 15+) / FS 2(目标 7+) / CPU 单核(目标 SMP)。
