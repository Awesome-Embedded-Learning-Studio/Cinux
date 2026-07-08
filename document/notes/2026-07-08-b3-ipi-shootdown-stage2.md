# B3 defect C — IPI TLB Shootdown deferred 集成（stage2）

**日期**：2026-07-08
**分支**：feat/boost_cinux（commit 待提，叠 stage1 `a330e3c` 之后）
**前序**：stage1 基建（`a330e3c`，见 `2026-07-08-b3-ipi-shootdown-stage1.md`）

## Context

stage1 建了 IPI shootdown 基建（vector 0xE1 + `send_ipi_all_others` + `tlb_shootdown_page` sync API + mechanism test），但**未集成 CoW**——因为 sync 直接挂 `handle_cow_fault` 有确定性死锁（两 CPU 同 CoW 互锁，见 stage1 note）。

stage2 做 **deferred 集成**：`handle_cow_fault` 不直接 free old_phys，推 pending list；drain kthread（IF=1）做 sync shootdown + free。治 defect C（跨核 stale TLB → free 后复用 → corrupt）。

## 改动

- **`pmm.hpp/cpp`**：加 `pte_count_dec_and_test_no_free` + `refcount_dec_and_test_no_free`（同原 `_dec_and_test` 逻辑 + audit，但**不** `buddy_.free`，返回 true 表示"该 free 但没 free"）。drain kthread 用现有 public `free_page`（L198，封装 lock + buddy_.free）。**audit 不推迟**（坏 free 瞬间仍 dec 时 panic）。
- **`tlb.hpp/cpp`**：加 `PendingShootdown{phys,vaddr,next}` + `g_pending_sem`（Semaphore）+ `g_drain_active`（bool，初始 false）+ `enqueue_pending_shootdown`（检查 `g_drain_active`：false→inline `free_page` 同 stage1；true→`kmalloc` 节点 + 插 list + `sem.post`）+ `dequeue_pending_shootdown`。`post` 用 `guard`（不动 IF），IF=0 安全；`wait` 用 `irq_guard` + `schedule_blocked`（需 scheduler）。
- **`tlb_drain.cpp`（新，`CINUX_TLB_DRAIN=ON` 默认）**：drain kthread `tlb_drain_entry`（`sem.wait` + 循环 `dequeue` → `tlb_shootdown_page` → `free_page` → `kfree`）+ `start_tlb_drain_thread`（设 `g_drain_active=true` + `TaskBuilder` priority 0 spawn）。**不 sti/hlt**（stats_kthread §14 警告：band-0 sti/hlt 饿死其他 band-0），用 `Semaphore::wait`（schedule out）。
- **`tlb_drain_stub.cpp`（OFF）**：空 `start_tlb_drain_thread`（`g_drain_active` 保持 false → inline free）。
- **`options.cmake`**：`option(CINUX_TLB_DRAIN ... ON)`（默认 ON，修 defect C；OFF 降级 inline free）。§14 file gate（无 source #ifdef）。
- **`arch/CMakeLists.txt`**：`if(CINUX_TLB_DRAIN) tlb_drain.cpp else stub`。
- **`process_new.cpp` `handle_cow_fault` L118**：`pte_count_dec_and_test` → `pte_count_dec_and_test_no_free` + 若 true `enqueue_pending_shootdown(old_phys, fault_vaddr)`。保留 L110 本地 `flush_tlb`（新映射生效）。
- **`init.cpp kernel_init_thread`**：`start_stats_thread()` 旁加 `start_tlb_drain_thread()`（production only，需 scheduler）。

## 关键设计：g_drain_active gate

`handle_cow_fault` 始终调 `_no_free + enqueue`。`enqueue` 检查 `g_drain_active`：
- **false**（suite-only test kernel 不调 `init.cpp`/`start_tlb_drain_thread`；或 pre-init；或 `CINUX_TLB_DRAIN=OFF`）→ inline `free_page`（无 shootdown，同 stage1 行为，单核安全）。
- **true**（production `start_tlb_drain_thread` 后）→ push + post（drain kthread shootdown + free）。

这让 test kernel（无 scheduler，不能 `Semaphore::wait`）安全跑（inline free，不回归），production 走 deferred。

## 为什么 drain kthread 不死锁（核心论证）

- drain kthread 跑在 scheduler 上下文 **IF=1**（`Semaphore::wait` 切走，被唤醒切回 IF=1）。
- 调 `tlb_shootdown_page` spin 等 ack 时，目标 CPU 若在 `handle_cow_fault`（IF=0）→ #PF **必退出**（IF 恢复）→ 处理 pending 0xE1 → ack。drain spin 终止。
- drain kthread **不持 fault 路径需要的锁**：PMM lock 只在 `free_page` 内短持（释放后才下个 shootdown）；`g_pending_lock` 短；`g_shootdown.lock` 单 in-flight。fault 路径的 PMM lock（`alloc_page`/`_no_free`）短，**释放后才 enqueue**。无锁环。

## 验证

`build-verify`（`CINUX_USE_KVM=ON`，`CINUX_TLB_DRAIN=ON` 默认）双 leg `run-kernel-test-all` 全绿：

- 单核 leg：全过。
- `-smp 2` leg：`[F-VERIFY] shootdown IPI test: PASS (all APs acked)` + `ALL TESTS PASSED`。
- test kernel 不调 `init.cpp`/`start_tlb_drain_thread` → `g_drain_active=false` → `enqueue` inline `free_page` → 不回归。`tlb_drain.cpp` 编入（编译验证）但 drain kthread 不启动。

**deferred 路径（production drain kthread + Semaphore::wait + shootdown + free）未跑验**——test kernel 无 scheduler，不能 `Semaphore::wait`，且无 fork（不能 stress CoW）。留 **smoke/production 验证**：用户启 GUI/-smp 2 run，drain kthread 启动（`[TLB] drain kthread started`），fork+CoW 走 deferred。

## GOTCHA

1. **`g_drain_active` gate 是 suite-only 安全的关键**：不加 gate，test kernel 链 `tlb_drain.cpp` 但 `start_tlb_drain_thread` 不被调（不调 `init.cpp`），`g_drain_active=false` → inline free。若 `start_tlb_drain_thread` 在 test kernel 被调（不该），drain kthread `Semaphore::wait` → `schedule_blocked` → 无 scheduler 崩。gate 确保即使 `start` 没调，`enqueue` 也安全 inline free。
2. **`Semaphore::post` 在 IF=0 安全**：用 `guard`（不动 IF）+ `Scheduler::unblock`（改 run queue，不 schedule）。`wait` 才需 scheduler（`schedule_blocked`）。
3. **drain kthread 不 sti/hlt**：stats_kthread §14 警告 band-0 sti/hlt 让 tick 抢致其他 band-0 饿死。用 `Semaphore::wait`（schedule out，让出 CPU，不饿死）。
4. **`pte_count_dec_and_test_no_free` 不 `store pte_count=0`**：原 `refcount_dec_and_test` L309 store 0 是防御性 reset（audit 已检查 pte_count==0）。no_free 不 store（已 0），drain `free_page` 也不 store（已 0）。等价。
5. **`free_page` 已 public**（pmm.hpp L59/L198）：封装 lock + buddy_.free，drain 直接用，不需新 `free_phys`。
6. **OOM 降级**：`kmalloc` 失败 → 直接 `free_page`（不 shootdown，defect C stale RO read，不 corrupt）。罕见。

## 缺陷 C 完整修复总结

- stage1（`a330e3c`）：IPI shootdown 基建（vector 0xE1 + all-excl-self + sync `tlb_shootdown_page` + mechanism test）。零行为改变。
- stage2（本批）：deferred 集成（`_no_free` + pending list + drain kthread + `handle_cow_fault` 改）。defect C 真修。
- **未集成路径**（execve/addr_space/sys_mmap/sys_shm ×2，IF=1 syscall）仍 inline free——sync 集成它们是 follow-up（工程小，无死锁，因 sender IF=1）。
