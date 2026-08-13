# NVMe IO 异步化(io_submit poll→IRQ-driven blocking)

> 日期：2026-07-16 · 分支 `feat/nvme-async-io` · B0-B2 ✅(test poll)，production IRQ 待 boot 验证
>
> **2026-08-12 更新**：B3 已完成，production SMP 卡死的设备/调度/同步取证与根修见
> [收官 note](2026-08-12-nvme-preempt-stall-root-fix.md)。下文保留 B2 当时状态作历史记录。

## 背景

NVMe IO 是同步 poll（`io_submit` 轮询 CQ 占满 CPU）。gcc 编译时 `io_submit` 持 `NvmeBlockDevice::lock_` 占满 CPU，别的 shell 卡死（[2026-07-11 yield revert note](2026-07-11-nvme-io-submit-yield-revert.md) 留的 v1.1+ 债）。本弧改 IRQ-driven blocking。

## 设计

**单 IO in-flight 不变**（不追吞吐，只治饿死）。核心：

- `io_submit` 提交 SQE（持 `io_lock_` Spinlock 短段）→ `Semaphore::wait` 阻塞等 completion → 醒返回。
- `nvme_irq_handler`（@IST2，EOI 由 ISR_IRQ stub 拥有）填实：`poll_one_completion`（解析 CQ + 推进 head）+ `Semaphore::post` 唤醒。
- `NvmeBlockDevice::lock_` Spinlock→**Mutex**（要跨 `schedule_blocked`；Mutex 不入 lockdep held 栈，持锁 sleep 不 panic）。

**为何不 yield**：`io_submit` 持 `NvmeBlockDevice::lock_` 跨 NVMe IO（护单 `dma_buf_`）。Spinlock 持锁 yield → lockdep「spinlock held across schedule」panic（2026-07-11 验证 revert）。治本是 poll→`schedule_blocked` 让 CPU，而单 `dma_buf_` 要求 IO 串行（持锁），故 `lock_` 改 Mutex（可持锁 sleep）。

**防 lost-wakeup**：Semaphore count（`post` 先于 `wait` 时 count>0 立返）。`io_pending_cid_` 在 doorbell 写入前设（单 in-flight 保证 IRQ handler poll 对的命令）。

**production/test 双模式**：test kernel IF=0 + MSI-X masked，IRQ 不 fire。`io_irq_mode_` 门控：production（`enable_async_irq` unmask + true）走 `sema.wait`；test（默认 false）走 poll。`poll_one_completion` 抽出共享给两路径 + IRQ handler。

## 实现（B0-B2）

- **B0**（`4c03874`）：立项 docs（PLAN/ROADMAP/todo 范围栅栏）。
- **B1**（`9a96218`）：抽 `poll_one_completion`（io_submit CQ poll 逻辑共享）+ `NvmeBlockDevice::lock_`→Mutex。行为不变，两腿 934/0 绿。
- **B2**（`7d63c13`）：`nvme_irq_handler` 填实 + `enable_async_irq`（`program_vector` unmask entry 0/1）+ `io_submit` 双模式 + main 接线（`set_active_nvme_ctrl` + `enable`）。test poll 两腿 934/0 绿。

## 验证状态

- ✅ **test 模式**（`io_irq_mode_=false`，poll）：`run-kernel-test-all` 两腿 934/0，NVMe round-trip 绿。
- ⏳ **production IRQ 模式**（`io_irq_mode_=true`）：`run-kernel-test-all` **不覆盖**（test kernel IF=0 + 不走 main.cpp `enable_async_irq`）。**待 boot 验证**。

## production boot 验证步骤（用户）

1. `cmake --build build --target run -j$(nproc)` + VNC 看 boot。
2. 启动日志应有 `[NVMe] async IRQ mode enabled (MSI-X unmasked, vector 0x41)`。
3. NVMe mount 成功（`[INIT] NVMe ext2 mount` 或 AHCI 兜底）。
4. 桌面开多终端，一个跑 `gcc hello.c`，别的 shell 应**不卡**（对比改前卡死）。
5. `[NVMe] NVM I/O ... failed` / `CQ timeout` 不应出现（IRQ 路径正常）。

若 boot 挂（panic/死锁），最可能：Mutex 持锁 `schedule_blocked` 的 lockdep（若 lockdep 实际跟踪 Mutex）/ IRQ handler 跨 CPU 竞态 / Semaphore 时序。**回退**：main.cpp 不调 `enable_async_irq`（`io_irq_mode_` 保持 false，poll 模式）= B1 状态。

## 风险/留续

- **production IRQ 路径未 boot 验证**（本弧最大留续）。
- 无超时兜底：`sema.wait` 无超时版，信 IRQ（NVMe 完成必 fire）。设备完全无响应才死锁（罕见）。
- 多 IO in-flight / 多 dma buffer / SQ 多 slot：远期（吞吐提升）。
- NVMe `0x4080` 取证：独立弧（IRQ 路径也 fire error completion，handler poll 拿 error status + post，不阻塞）。
