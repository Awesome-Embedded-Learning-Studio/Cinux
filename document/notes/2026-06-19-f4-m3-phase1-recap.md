# F4-M3 Phase 1 收尾:per-CPU 架构(单核重构)

> 2026-06-19。F4-M3 Phase 1(P1-1~4)完成。**Phase 2(AP 启动)待 Phase 1 落地后再议。**
> 分支 `feat/f4-m1-acpi`。基线 869/0 → 869/0(全程行为不变)+ 真机 GUI。

## Phase 1 成果
把 per-CPU 数据/结构从静态全局迁到 **GS-based per-CPU 控制块**,为 Phase 2 多核铺地基。全程**单核行为不变**(869/0 贯穿),是「纯重构 + 多核就绪」层。

| 批 | Commit | 核心 |
|----|--------|------|
| P1-1 | eaccc57 | PerCpu 结构(kernel_stack@0)+ percpu_blocks[] + percpu() 静态返 [0];迁移 ~15 处 g_per_cpu + 4 测试文件;gs 页双镜像过渡;删 per_cpu.hpp/g_per_cpu |
| P1-2 | c1a511e | GS base 锚定 PerCpu[0] + 完整 swapgs 纪律(ISR 条件 swapgs / jump_to_usermode swap / context_switch 去 GS / percpu 读 MSR / msr.hpp / usermode_init 提前) |
| P1-3 | b9af79f | g_gdt → gdt_blocks[kMaxCpus];tss_set_rsp0 走 percpu()->cpu_id(签名不变) |
| P1-4 | (本笔记 + ROADMAP/PLAN) | 全回归 + test_host + 真机 + 收尾 |

## 关键不变量(Phase 1 后)
- syscall `%gs:0` = PerCpu.kernel_stack(GS 指向本 CPU 块)。
- `percpu()` 读 `MSR_GS_BASE` = 本 CPU 块(内核态任意上下文安全,含中断)。
- swapgs 纪律:内核态 GS_BASE=K/KERNEL_GS_BASE=0;syscall entry/exit、ISR 条件(按 CS 判 CPL=3)、jump_to_usermode 各 swap。
- 每 CPU 独立 GDT/TSS(gdt_blocks[]),rsp0 不互踩。
- FS_BASE(TLS)per-task 存取不变;context_switch 只存 fs_base。

## Phase 1 调研中最有价值的发现(P1-2)
**原设计文档低估了 swapgs 牵连**——ISR(interrupts.S)无 swapgs(仅 syscall.S 有),中断从用户态进入 GS_BASE=0 而 schedule()→percpu() 在中断上下文。让 percpu() 读 MSR 必须先建完整 swapgs 纪律(ISR 条件 swapgs)。**这是设计稿核对代码基线时抓到的、文档没预见的硬约束**——也是为何本会话先出设计稿核对、而非直接照设计文档执行的价值。

## 验证(Phase 1 全量)
- `timeout 40 run-kernel-test`:**869/0**(P1-1/2/3 每批 + format 后重验)。
- `cmake --build build`(全量,含 image/test_fork_exec/big_kernel_test)+ `test_host`:全绿(CI 盲区覆盖)。
- `timeout 40 cmake --build build --target run`:真机每批冒烟,启动到 GUI 桌面(PIT/syscall/键鼠经用户态中断 swapgs 路径全验证)。无 panic/GP/PF。

## 已知局限(留 follow-up,不阻塞 Phase 2)
- **NMI/#DB 在 syscall-exit swapgs 窗口**(swapgs 后→SYSRET 前,GS_BASE=0 但仍内核态):若此刻 NMI/#DB handler 调 percpu() 读 0。窗口极窄、NMI 罕见、handler 不调 percpu,实际风险极低。Linux paranoid NMI 路径留 follow-up。
- **lost-wakeup(futex/waitpid/mutex)**:Phase 1+2 不修,留 Phase 3/M4(单核 + AP idle 不暴露)。

## 下一步
Phase 2(AP 启动,设计文档 P2-1~5):IPI + ap_trampoline @0x8000 + ap_main + INIT-SIPI-SIPI + `-smp 2`。Phase 1 已就绪(percpu GS/gdt_blocks/swapgs 纪律),Phase 2 启动 AP 时各设自己的 KERNEL_GS_BASE=&percpu_blocks[cpu] + 加载 gdt_blocks[cpu] + 填 cpu_id/apic_id 即可接入。待用户决定 Phase 2 时机。
