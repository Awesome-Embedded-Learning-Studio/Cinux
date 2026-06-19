# F4-M3 P1-1:PerCpu 控制块 + percpu() 访问器

> 2026-06-19。F4-M3 Phase 1 批 1(P1-1)。执行依据:`document/notes/2026-06-19-f4-m3-design.md`。
> 基线 main(含 F4-M1+M2,869/0)→ 869/0 + 真机 GUI 启动到桌面(行为不变)。
> 分支 `feat/f4-m1-acpi`,commit `eaccc57`。

## 目标
把 per-CPU 数据从**静态全局** `g_per_cpu`(单一 `PerCPU` 实例)迁到 **per-CPU 控制块数组** `percpu_blocks[kMaxCpus]` + `percpu()` 访问器,为 Phase 2(GS base 指向 percpu 块、AP 启动)铺地基。**P1-1 不碰 GS / context_switch / syscall 机制**——单核行为完全不变,是「纯重构」绿检查点。

## 实现

### 新增 `kernel/proc/percpu.hpp` / `percpu.cpp`
```cpp
struct PerCpu {
    uint64_t kernel_stack;   // @0  syscall %gs:0
    uint64_t scratch1;       // @8  syscall %gs:8
    uint64_t scratch2;       // @16 syscall %gs:16
    Task*    current;        // @24
    uint32_t cpu_id;         // @32
    uint32_t apic_id;        // @36
};
alignas(4096) PerCpu percpu_blocks[kMaxCpus];  // kMaxCpus = 8
PerCpu* percpu();            // P1-1: &percpu_blocks[0](静态);P1-2 改读 MSR_GS_BASE
```
- **`kernel_stack @0` 是硬契约**:syscall_entry 从 `%gs:0` 载内核栈。4 个 `static_assert`(offset 0/8/16/24)锁布局,防未来字段编辑静默 break syscall。
- `percpu()` P1-1 静态返 `&percpu_blocks[0]`(单核,GS 未动);P1-2 改读 `MSR_GS_BASE`。
- **gs 页双镜像**(P1-1 过渡):syscall 仍从 `usermode_init` 单独分配的 gs 页读 `%gs:0`(KERNEL_GS_BASE 仍指 gs 页,本批不动)。故 `update_syscall_stack` 同时写 `percpu()->kernel_stack` 和镜像到 gs 页[0]。gs 页由 `usermode_init` 经 `set_gs_mirror()` 注册。P1-2 才把 KERNEL_GS_BASE 直接指向 percpu 块、消除 gs 页。
- 删 `per_cpu.hpp` + `g_per_cpu` 全局(`scheduler.cpp` 的定义 + `update_syscall_stack` 内联方法)。

### 迁移(~15 生产点 + 4 测试文件)
- `g_per_cpu.current` → `percpu()->current`(scheduler/sync/sys_futex)。
- `g_per_cpu.update_syscall_stack(x)` → `update_syscall_stack(x)`(scheduler×3/init/gui_init,改自由函数)。
- `g_per_cpu.gs_page_vaddr` → `gs_mirror_vaddr()`(fork/clone/task_builder 的 `ctx.kgs_base =`)。
- `g_per_cpu.gs_page_vaddr = x` → `set_gs_mirror(x)`(usermode_init)。
- 测试 `g_per_cpu.current = t` → `percpu()->current = t`(test_sync/test_sync_concurrent/test_futex/test_clone,sed 批量)。

## 关键不变量(重构后保持)
- syscall `%gs:0` == PerCpu.kernel_stack == current task 内核栈顶(经 gs 页镜像,P1-1)。
- `Scheduler::current()` 仍读静态 `current_`(P1-2 才改读 `percpu()->current`)。
- `Scheduler::set_current` 双写 `current_` + `percpu()->current`。

## 决策要点
1. **测试用 `percpu()->current = t` 而非 `Scheduler::set_current(t)`**:忠实 1:1 迁移旧 `g_per_cpu.current = t`(只写 percpu,不动 `current_`),零行为变化。这些测试(Mutex/futex 读 percpu、block 用传入 task)不依赖 `current_`,GOTCHA#21 的 set_current 警告针对的是依赖 `current_` 的 handler(sys_setpgid 等),此处不触发。
2. **删 per_cpu.hpp 换 percpu.hpp/cpp**:命名对齐 `percpu()` 访问器;一处一职责(CODING-TASTE)。
3. **保留 gs 页双镜像**:P1-1 必须保 syscall 契约——GS 未动,syscall 仍读 gs 页,故 update_syscall_stack 双写。这是设计文档未点透的执行细节(见 plan 修订#1)。

## 验证
- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`:**869 passed, 0 failed**。
- `timeout 40 cmake --build build --target run`:真机启动 AHCI→ext2→VFS→GUI 桌面→mouse→gui_worker,无 panic/segfault(signal 15 = timeout 杀 QEMU,预期)。syslog 经 gs 镜像路径工作。

## 下一步
P1-2(高危,一批完成 + 真机必做):GS base → PerCpu[0]——`usermode_init` 改 `KERNEL_GS_BASE = &percpu_blocks[0]`、删 gs 页镜像;`context_switch.S` 删 GS 存取(fs_base 保留);CpuContext gs/kgs 留 reserved、删 3 处 `kgs_base=`;`percpu()` 改读 MSR_GS_BASE;补 `msr.hpp` 的 `read_msr`。
