# F-VERIFY M3 — SMP 测试唤醒基建（收官 2026-06-27）

> 横切里程碑 F-VERIFY 第三批。分支 `feat/f-verify`。M0/M1 见前两篇 note。
> **本批破解 audit 头号盲区：47/47 QEMU-SMP 空转。**

## 目标

`run-kernel-test-smp` 虽带 `-smp 2`，但测试内核从不唤醒 AP、不起调度器 → 套件 BSP-only 跑，SMP 门**名存实亡**。所有 SMP-only bug（LSTAR-AP==0 #DF、迁移竞态、CoW 跨核 UAF）CI 永远抓不到。M3 让 AP 在测试内核里**真唤醒 + 跑回读 + 向 BSP 汇报**，把空转门变真。

## 打法（分两子批，先地基后唤醒）

### M3-1：测试内核探测真实 SMP 拓扑（`141b093`，不碰生产）

测试内核原 only 在手造表上测 MADT parser（`run_acpi_tests`），从不扫固件真表 → `g_acpi_info.cpu_count` 恒 0 → `boot_aps()` 直接 "single CPU" 返回。

**修**：测试内核 [main_test.cpp](../../kernel/test/main_test.cpp) 在 direct-map probe 后加真 `drivers::acpi::init()`（扫固件 RSDP/MADT 填 `g_acpi_info`）+ 打印拓扑。
**验证**：单核 leg `cpu_count=1`；`-smp 2` leg `cpu_count=2 apic_id[0..1]=0,1`。M3-2 唤醒 AP 的前置。

### M3-2：AP 唤醒 + AP 侧机制回读（`03201e8`，碰生产但 gated）

**核心设计——一个 default-off 钩子，生产字节不变：**
- 生产侧 [smp.hpp](../../kernel/arch/x86_64/smp.hpp)：加 `ApSelfcheckResult` 结构（cr4/efer/lstar/star/sfmask + magic）+ `kApSelfcheckMagic=0xA5C0FFEE` + `extern g_ap_selfcheck_results[]` + `extern g_ap_test_selfcheck_fn`（默认 nullptr）。
- 生产侧 [ap_main.cpp](../../kernel/arch/x86_64/ap_main.cpp)：AP 在 `g_aps_online++`（online 信号）**之后**、scheduler 自旋**之前**插 `if (g_ap_test_selfcheck_fn != nullptr) { fn(cpu_id); for(;;) cli;hlt; }`。**fn=null（生产）分支跳过，零行为变化**；AP 跑完回读后 halt 绕开 scheduler（测试内核不起调度器）。
- 测试内核：`ap_test_selfcheck`（AP 上读 CR4/EFER/LSTAR/STAR/SFMASK→结果槽，magic 最后写，x86 TSO 保 BSP 读到完整槽）+ `run_smp_ap_wake_test`（LAPIC IPI init + 设 fn + `boot_aps()` + BSP 轮询 magic + 断言 lstar!=0 / CR4 OSFXSR·OSXMMEXCPT / EFER.NXE），OR 进 `exit_code`。单核（cpu_count<=1）跳过返 true。

**关键时序**：`g_aps_online` 在 ap_main 匿名 namespace（外部不可见），故 BSP **不读 online 计数**，改轮询每个 AP 的 `result.magic`（AP 写完回读才置 magic，天然同步，无需 fence）。

## 验证（run-kernel-test-all）

- 单核 leg：`[F-VERIFY M3-2] single-CPU: AP wake test skipped`（smp_ok=true，**零回归**）。
- `-smp 2` leg：
  ```
  [F-VERIFY M3-2] booting 1 AP(s) + readback...
  [AP1] GS anchored
  [AP1] online (apic_id=1)
  [F-VERIFY M3-2] AP1: magic=0xa5c0ffee cr4=0x300620 efer=0xd01 lstar=0xffffffff8106d961
  [F-VERIFY M3-2] AP wake + readback: PASS
  ```
- CR4=0x300620 = PAE(5)\|OSFXSR(9)\|OSXMMEXCPT(10)\|**SMEP(20)\|SMAP(21)**；EFER=0xd01 = SCE(0)\|LME(8)\|LMA(10)\|NXE(11)；LSTAR=&syscall_entry 非零——**AP 与 BSP 完全对齐**。
- 两 leg 945/0 + 954/0，exit 0。

## 交付（一石三鸟）

1. **破解 47/47 QEMU-SMP 空转**——run-kernel-test-smp 从空门变真门。
2. **AP 侧机制回读落地**（M4 的 AP 列同步交付）——SMEP/SMAP/NXE/LSTAR 在 AP 上读回断言，SMEP/SMAP "4 批假绿" + LSTAR==0 #DF（F5-M5 GOTCHA）类现 CI 立抓。
3. **证明 gated-钩子范式**——生产 SMP 代码（ap_main，GOTCHA 最密文件）用 default-off 分支接入测试，零生产行为变化，是后续 M5（cross-core 真任务压力）的可复用模式。

## 教训 / GOTCHA

- **`run-kernel-test-smp` ≠ "测了 SMP"**：带 `-smp 2` 但不 boot_aps = 空转。M3 前所有"SMP 绿"都不证明 AP 路径。`run-kernel-test-all`（F-VERIFY 统一入口）现在两 leg 都跑，M3 让 -smp leg 真有料。
- **匿名 namespace 全局外部不可见**：`g_aps_online` 在 ap_main 匿名 ns，测试内核读不到 → 用结果槽的 magic 字段做 BSP↔AP 同步（比暴露 online 计数更干净：magic 写在回读之后 = AP 真跑完）。
- **aggregate 不能从 volatile 源拷贝**：`ApSelfcheckResult r = *volatile_slot` 编不过（隐式拷贝 ctor 接不了 volatile ref）→ 逐字段读（诊断 caught，没进 build）。
- **M3 没踩坑**：M3 是"F-VERIFY 最硬一批"（重构 harness 启动模型 + 碰生产 SMP），但 gated 钩子 + 先证零回归 + 一次过的设计，让它**没变成它要防的那种多会话坑**——这正是 F-VERIFY 文化的体现。

## 下一步

M3 收官（enabler 达成）。跨核**真任务压力**（AP 跑 fork/CoW/迁移，需 scheduler-light）折入 **M5**（真用户 fork/CoW 压力回归——那里本来就需要调度器跑真用户任务）。
- **M4** 机制回读矩阵（BSP 侧其余位 + 把 M3-2 的 AP 回读正式登记进 matrix）+ 并发检测（host TSAN / KCSAN-lite）。
- **M5** 真用户 fork/CoW 压力回归 + 继承 AS execve（headline，复用 M3 的 AP 唤醒做 cross-core）。
- **M6** 故障可观测增强。

feat/f-verify 当前 10 commit（M0×5 + M1 + 统一入口 + M3-1 + M3-2 + 本批 docs）。push/PR 归用户（整个 F-VERIFY 一个大 PR）。
