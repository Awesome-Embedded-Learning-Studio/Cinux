# B3 defect C — IPI TLB Shootdown 基建（stage1）

**日期**：2026-07-08
**分支**：feat/boost_cinux（commit `a330e3c`，未 push）
**前序**：B3 mm refcount 收尾（A/B/D/E/F 已修 + 审计常驻，见 `b3-ipc-refcount-handoff` memory）

## Context

B3 defect C：`handle_cow_fault`（`kernel/proc/process_new.cpp:72-121`）L110 本地 `flush_tlb` 后 L118 `pte_count_dec_and_test(old_phys)` 直接 free。`-smp` 下另一核 TLB 可能仍缓存 old_phys 的 stale 映射 → free+复用 → stale read/corruption。CinuxOS 无 IPI shootdown 基建。

memory 原设计是 **sync 直接挂 handle_cow_fault**（单 in-flight lock + sender 发 IPI all-excl-self + spin 等 acks==0）。**经核实有确定性死锁**，不能照搬：

- `handle_cow_fault` 在 #PF ISR 内，全程 IF=0（interrupt gate + IST2 防嵌套；sti 会复用 IST2 覆盖帧，即 F13-B Bug② 教训）。
- 两 CPU 同时 CoW 同一 phys（fork 后父子都写同页，`-smp 2` 确定性可复现）→ A 持 shootdown lock 发 IPI 给 B、spin 等 ack；B 阻在 shootdown lock 上 spin（IF=0）**不处理 A 的 IPI** → 互锁。
- `Spinlock::guard()` 不动 IF（`sync.cpp:21-33`），是 `IrqGuard` 才 cli —— 等锁时 IF 保持 0，hole 成立（Plan agent 对照源码四点全部确认）。

**正解：deferred free + drain kthread**。`handle_cow_fault` 把 old_phys 推 pending list 不 free；drain kthread 在 IF=1 上下文做 sync shootdown 再 free。不死锁。分两批：**stage1 = 基建（零行为改变，本批）**，stage2 = deferred 集成（defect C 真修，后续）。

## 本批改动（stage1 基建）

照 reschedule IPI（0xE0）套路逐 vector 落：

- **`smp.hpp`**：`kShootdownIpiVector = 0xE1`（挨 `kRescheduleIpiVector` 0xE0；避 PIC 0x20-0x2F / spurious 0xFF / sigreturn 0x80）。
- **`local_apic.{hpp,cpp}`**：ICR 目标 shorthand 常量 `kIcrShorthandAllExclSelf = 0b11u << 18`（ICR bits[19:18]=11）+ `send_ipi_all_others(vector)`（ICR high=0 忽略 dest + poll delivery status idle + ICR low = vector | kIcrModeFixed | kIcrShorthandAllExclSelf）。
- **`tlb.{hpp,cpp}`（新）**：file-scope `g_shootdown{Spinlock lock; uint64_t vaddr; uint64_t acks_remaining}` + `tlb_shootdown_page(vaddr)`（单 in-flight：`lock.guard()` → 设 vaddr/acks=`online_ap_count()` → 本地 `flush_tlb` → 若 acks>0 `send_ipi_all_others(0xE1)` → spin ACQUIRE 等 acks==0）+ `shootdown_ipi_handler`（`extern "C"`，invlpg(vaddr) RELAXED + atomic sub acks ACQ_REL；stub own EOI）。
- **`interrupts.S`**：`ISR_IRQ shootdown_ipi_stub, shootdown_ipi_handler, 0`（挨 reschedule stub；`\irq=0` unused APIC-only）。
- **`irq_handlers.cpp`**：stub decl + `g_idt.set_handler(0xE1, shootdown_ipi_stub, GDT_KERNEL_CODE, kIRQAttr, 2)`（IST=2，挨 reschedule 注册）。
- **`arch/CMakeLists.txt`**：加 `tlb.cpp`。
- **`main_test.cpp`**：机制 test 合并进 `run_smp_ap_wake_test`（共享 `boot_aps` 避免二次 boot 冲突）。suite-only 分支：AP 在 `ap_test_selfcheck` 里 mask LAPIC timer（bit 16，防 sti 后 timer fire 调 `Scheduler::tick` 崩——test kernel 无 scheduler）+ `sti;hlt` 等 IPI；BSP 在 magic 轮询后调 `tlb_shootdown_page(0xDEADB000)` → spin acks==0。smoke 分支跳过（AP 进 scheduler，时序复杂，留 production 自证）。

## 验证

`build-verify`（CINUX_USE_KVM=ON）双 leg `run-kernel-test-all` 全绿：

- 单核 leg：mechanism test no-op（`ap_count==0`），全绿。
- `-smp 2` leg：`[F-VERIFY] shootdown IPI test: sending to 1 AP(s)` → `PASS (all APs acked)` → `ALL TESTS PASSED`。IPI 0xE1 收发端到端验证通过（AP mask timer + sti;hlt 响应，handler invlpg + dec acks，BSP spin 归 0）。

## GOTCHA

1. **TCG（无 KVM）下 `-smp 2` `test_production_ping` 卡死**：单核 TCG PASS，`-smp 2` TCG 卡在 SLIRP reply 投递（既有 timing 问题，memory f5-m6 记录）。用 `CINUX_USE_KVM=ON`（WSL2 `/dev/kvm` 可用，memory `kvm-available-wsl2`）即过。**非本批引入**（单核 TCG 全绿 + ping 在 mechanism test 之前 + 我的改动单核 no-op；KVM 下 -smp2 ping PASS）。
2. **`shootdown_ipi_handler` 必须 `extern "C"`**：`ISR_IRQ` asm stub 用 C symbol 调用（照 `reschedule_ipi_handler` 在 `ap_main.cpp` 是 `extern "C"`）。初版漏加 → 链接 undefined reference（C++ mangled 名）。修：hpp decl + cpp def 都 `extern "C"`。
3. **pre-commit `check_line_limits --hpp 500`**：`interrupts.S` 加 stub + 注释块到 507 行 > 500 被拦。精简注释块（10 行 → 2 行）到 499。stub 代码不变，二进制一致。
4. **mechanism test 不能独立 `run_shootdown_mechanism_test`**：会与 `run_smp_ap_wake_test` 二次 `boot_aps` 冲突（已 boot 的 AP 再 SIPI 失败）。合并进 `run_smp_ap_wake_test`（共享一次 boot）。
5. **AP `sti` 前必须 mask LAPIC timer**：`ap_main` L173 无条件 arm ~300Hz timer，test kernel 无 scheduler，sti 后 timer fire → `lapic_timer_handler` → `Scheduler::tick` 崩。LINT0 默认 masked（test kernel 未配），故 sti 后只收 IPI 不收 PIC IRQ，安全。

## 为什么 drain kthread 不死锁（stage2 核心论证，留此备查）

- drain kthread 跑在 scheduler 上下文 **IF=1**（`net_poll_entry` / `stats_kthread` 证 kthread IF=1 可行）。
- 调 `tlb_shootdown_page` spin 等 ack 时，目标 CPU 若在 `handle_cow_fault`（IF=0）→ #PF **必退出**（IF 恢复）→ 处理 pending 0xE1 → ack。drain spin 终止。不互锁。
- drain kthread 不持 fault 路径需要的锁：PMM lock 只在 `free_phys` 内短持；`g_pending_lock` 短；`g_shootdown.lock` 单 in-flight。fault 路径的 PMM lock 短，**释放后才 enqueue**。无锁环。

## stage2 计划（defect C 真修，后续批次）

- 拆 `pte_count_dec_and_test` → `_no_free` 变体（dec + audit，不 `buddy_.free`）+ public `PMM::free_phys`（封装 store pte_count=0 + lock + buddy_.free）。
- pending list + `Semaphore`（`sync.cpp:159-191`）+ drain kthread（照 `stats_kthread.cpp`，priority 0，`Semaphore::wait` 不 sti/hlt）+ file-gate stub。
- `handle_cow_fault` L118 改 `_no_free` + `enqueue_pending_shootdown(old_phys, fault_vaddr)`。
- `init.cpp` 启动 drain kthread。
- stress test：`-smp 2` fork+CoW 同页（复刻死锁场景，验证 deferred 不死锁 + 无 corrupt）。需 smoke/production 跑（test kernel 无 scheduler，fork 不工作）。
