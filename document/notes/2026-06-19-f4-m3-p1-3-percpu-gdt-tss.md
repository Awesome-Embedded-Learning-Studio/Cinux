# F4-M3 P1-3:per-CPU GDT/TSS 数组

> 2026-06-19。F4-M3 Phase 1 批 3(P1-3)。基线 P1-2(869/0)→ 869/0 + 真机 GUI。
> 分支 `feat/f4-m1-acpi`,commit `b9af79f`。

## 目标
`g_gdt`(单一 GDT 实例)→ `gdt_blocks[kMaxCpus]`(每 CPU 独立 GDT/TSS/RSP0/IST),消除多核 context switch 互踩 rsp0 的隐患(P0#3)。单核仍只用 [0],Phase 2 AP 各加载自己的。

## 实现
- **gdt.hpp**:`extern GDT g_gdt` → `extern GDT gdt_blocks[]`(**不完整数组声明**——免 gdt.hpp→percpu 耦合,arch 头不依赖 proc)。`tss_set_rsp0` 签名不变(`static void tss_set_rsp0(uint64_t)`),doc 注明它定位**本 CPU** 块。
- **gdt.cpp**:`GDT gdt_blocks[cinux::proc::kMaxCpus]`(定义带大小,include percpu);`tss_set_rsp0` 改 `gdt_blocks[percpu()->cpu_id].tss_.rsp[0] = rsp0`(内部读 percpu 定位本 CPU)。
- **调用点**:`g_gdt.init()` → `gdt_blocks[0].init()`(main/main_test,BSP)。**scheduler 4 处 + test_usermode 6 处 tss_set_rsp0 零改动**(签名不变,内部走 percpu)。test_usermode 删死 `using g_gdt` + 更新注释。

## 决策要点
1. **tss_set_rsp0 读 percpu()->cpu_id 内部定位,而非加 cpu 参数**:语义上「设本 CPU 的 RSP0」天然属于当前 CPU;免改 10 处调用点签名;代价是 gdt.cpp→percpu 耦合(可接受,percpu 是轻量头)。
2. **gdt.hpp 不完整数组声明**:extern array 无尺寸,gdt.hpp 不需 kMaxCpus → 不 include percpu → 保持 arch 头独立。仅 gdt.cpp 定义处需 kMaxCpus。

## 不变量
- BSP 用 gdt_blocks[0],percpu()->cpu_id=0,tss_set_rsp0 写 [0].tss_.rsp[0](等价旧 g_gdt)。
- 每 CPU 独立 TSS → 多核 context switch 不互踩 rsp0(Phase 2 兑现)。

## 验证
- `timeout 40 run-kernel-test`:**869/0**。
- `cmake --build build`(全量)+ `test_host`:全绿。
- `timeout 40 cmake --build build --target run`:真机 GDT loaded → PerCpu GS anchored → PIT → GUI 桌面 → mouse。无 panic/fault。

## 下一步
P1-4 收尾:Phase 1 全回归 + ROADMAP/PLAN(Phase 1 ✅)+ 收尾笔记。
