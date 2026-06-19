# F4-M3 P1-2:GS base 锚定 PerCpu 块 + 完整 swapgs 纪律

> 2026-06-19。F4-M3 Phase 1 批 2(P1-2)。基线 P1-1(869/0)→ 869/0 + 真机 GUI。
> 分支 `feat/f4-m1-acpi`,commit `c1a511e`。

## 目标
让 `percpu()` 读 `MSR_GS_BASE`(真正 per-CPU),GS base 指向 `percpu_blocks[cpu]`,为 Phase 2 多核铺路。**P1-2 是高危批**——破坏 syscall/中断的 GS 契约,中间态不可用。

## 关键发现:原设计低估了 swapgs 牵连
调研发现当前 swapgs 纪律**不全**:
- `syscall.S`:有 entry/exit swapgs(唯一处)。
- `interrupts.S` ISR 宏:**完全没有 swapgs**。
- `boot.S`:不设 GS_BASE(开机 = 0)。
- `usermode.S` jump_to_usermode:**无 swapgs**。

后果:中断(PIT/PF)从**用户态**进入时 GS_BASE=0,而 `pit→tick→schedule()→percpu()` 在中断上下文调 percpu()。P1-2 若让 percpu() 读 MSR,这些路径读到 0 → 解引用 0 → 崩。**原设计文档把 P1-2 当「只动 context_switch.S」,低估了 swapgs 范围。**

经用户确认走 **Option A(完整 swapgs 纪律,现在做对)**。

## 实现:swapgs 纪律(目标不变量)
- **内核态**:GS_BASE = K(&percpu_blocks[cpu]),KERNEL_GS_BASE = 0(用户 GS)。
- **用户态**:GS_BASE = 0,KERNEL_GS_BASE = K。
- 每次用户↔内核切换 swapgs 翻转。

### 改动
1. **msr.hpp(新)**:`read_msr`/`write_msr` inline + `kMsrGsBase`/`kMsrKernelGsBase` 常量。
2. **usermode_init**:去 gs 页分配;`write_msr(GS_BASE, &percpu_blocks[0])` + `write_msr(KERNEL_GS_BASE, 0)`;填 `cpu_id/apic_id=0`。`percpu()` 改读 `MSR_GS_BASE`。删 gs 页镜像(`set_gs_mirror`/`gs_mirror_vaddr`/`update_syscall_stack` 简化为单写)。
3. **jump_to_usermode**(usermode.S):SYSRET 前加 `swapgs`(出内核)。
4. **ISR 宏**(interrupts.S):两个宏各加**条件 swapgs**——按帧内 CS 判 CPL=3:
   - entry:压完 reg+padding 后、call 前,`movq 144(%rsp),%rax; testb $3,%al; jz 1f; swapgs; 1:`(CS 在 padding+15reg+error+RIP = +144)。
   - exit:除 padding 后、pop 前,`movq 136(%rsp),%rax; ...`(padding 除后 +136)。`%rax` scratch(栈上已存,pop 恢复)。`syscall.S` 已有 swapgs,新纪律下天然正确(不改)。
5. **context_switch.S**:删 GS 存/取(GS per-CPU 固定),留 fs_base(per-task TLS)。
6. **CpuContext**(process.hpp):`gs_base`/`kgs_base` 留 **reserved**(不删,保 offset/sizeof/static_assert 不变,降 churn);删 fork/clone/task_builder 的 `ctx.gs_base/kgs_base` 赋值 + 多余 percpu.hpp include。
7. **usermode_init 提前**(main.cpp + main_test.cpp):移到 IDT 之后(所有 percpu 使用 / 中断前)。**关键修复**:原 main_test 里 usermode_init 在 sync/futex/scheduler 测试之后——P1-2 后 percpu() 读 MSR,这些测试会读到 GS_BASE=0 崩;提前到 IDT 后解决。

## 不变量(验证后保持)
- syscall `%gs:0` = PerCpu.kernel_stack(GS 指向 percpu 块)。
- `percpu()` 读 MSR_GS_BASE = 本 CPU 块(内核态任意上下文安全)。
- 中断从用户态进入 → 条件 swapgs → GS_BASE=K,handler 可调 percpu()。
- 单核 BSP:percpu_blocks[0],GS 锚定一次。

## 已知局限(留 follow-up)
- **NMI/#DB 在 syscall-exit swapgs 窗口**:swapgs 后、SYSRET 前的数条指令内,GS_BASE 已=0(为用户态准备)但仍处内核态。若此刻 NMI/#DB 触发且其 handler 调 percpu() → 读 0。窗口极窄(~4 指令)、NMI 罕见、handle_nmi/db 不调 percpu(仅记日志),实际风险极低。Linux 用 paranoid NMI 路径解决——留 F4 follow-up(若启用 NMI 真用途再做)。

## 验证
- `timeout 40 run-kernel-test`:**869/0**。
- `timeout 40 cmake --build build --target run`:真机启动 AHCI→ext2→VFS→GUI 桌面→mouse→**GUI tick 注册到 PIT**(PIT 从用户态中断 → ISR 条件 swapgs 路径验证)→gui_worker。无 panic/GP/PF。signal 15 = timeout 杀 QEMU(预期)。
- 关键:ring0 测试覆盖不到的用户↔内核 swapgs 翻转,由真机 GUI(syscall/PIT/键鼠)验证通过。

## 下一步
P1-3:per-CPU GDT/TSS(`gdt_blocks[kMaxCpus]` + per-CPU `tss_set_rsp0`),单核仍只用 [0]。
