# NVMe 异步 IO + 内核抢占卡死根修

> 日期：2026-08-12 · 分支 `feat/nvme-async-io` · 状态：✅ production SMP 复现闭环

## 现象

production `-smp 2` 启动 GUI 与第二个 shell 时，NVMe 提交停在 `cid=259`，串口看起来像
“AP 写 doorbell 后，固定投 BSP 的 MSI-X completion 丢了”。同时 AP 只剩低优先级
`net_poll` 自选，shell 没有 prompt。热路径调度/IRQ 日志进一步改变了时序，曾把排查带向
IRQ affinity、idle 特判和 syscall-return 补调度点。

## 证伪“跨 CPU MSI-X 丢失”

QEMU NVMe trace 与 HMP 在同一次冻结中给出了完整设备侧证据：

1. QEMU 消费 `cid=259` 的 SQ doorbell；
2. 设备生成成功 CQE，并执行 `irq_msix raising MSI-X IRQ vector 1`；
3. BSP LAPIC `IRR` 中已有十进制 65（`0x41`），`ISR` 为空，TPR/PPR 为 0；
4. BSP `RFLAGS=0x2`（IF=0），RIP 在 `Spinlock::acquire()`；
5. 锁地址经符号和对象偏移定位为 production Ext2 的 `inode_cache_lock_`。

栈为 `execve -> vfs_lookup -> Ext2::get_cached_inode -> Spinlock::acquire`。另一个 CPU 的
任务持该锁，在 `read_disk_inode -> NVMe io_submit -> Semaphore::wait` 睡眠。于是 BSP 关中断
自旋，NVMe `0x41` 只能留在 IRR，不能唤醒持锁者。早先日志中多个 AP submit / BSP completion
已成功配对，也直接反证了“AP 提交天然不能向 BSP 投递”。

第一层修复因此是把 Ext2 inode cache / bitmap 锁和 `File::offset_lock_` 改为可睡眠 Mutex；
后者同样会跨 PageCache miss / 异步块 IO，不能用会绑定 per-CPU `preempt_count` 的 Spinlock。

## 最终根因：迁移后使用旧 PerCpu 指针

移除 Ext2 自旋后，复现从 BSP IF-off 自旋变成 `CQ CID mismatch got=N expected=N+1`。GDB 在
第二个请求产生的瞬间抓到：任务 B 在 contended `NvmeBlockDevice::lock_` 后未获得 ownership
却从 `Mutex::lock()` 返回，与仍在等待 completion 的任务 A 并发进入 `io_submit`。

旧代码的关键序列是：

```text
call percpu()             # RDMSR(GS_BASE)，返回 CPU-local PerCpu*
mov 24(%rax), %rax        # 稍后才读取 PerCpu::current
```

timer IRQ 可以落在两条指令之间，并在 IRQ-exit 抢占后把当前任务迁移到另一 CPU。恢复时寄存器
仍保存旧 CPU 的 PerCpu 指针，第二条指令读到旧 CPU 此刻运行的另一个任务。Mutex 因而把错误
Task 入队/标 Blocked；真实调用者仍为 Running，`schedule_blocked()` 后继续执行，破坏互斥和
NVMe single-inflight，不同运行中出现 `owner_ == wait_head_` 同一 Task 正是该链的结果。

根修把所有可抢占路径改为单条 `%gs:offset` 字段访问：`current_task()`、current setter、CPU id、
`need_resched`、`preempt_count` 与 syscall stack。最终反汇编的 `current_task()` 核心为
`mov %gs:0x18,%rax`，不再存在“先取 CPU 基址、后解引用”的迁移窗口。

## 同批收敛

- `schedule()`、`exit_current()`、`run_first()` 从第一条状态操作起用 `IrqSaveGuard` 关闭本地
  IRQ，保证 current/on_cpu/TSS/context switch 安装不可被本地 timer 重入；并常开检查
  `preempt_count==0`，让未来的持 Spinlock 睡眠立即 panic，而非跨 CPU 污染计数。
- Mutex 醒后循环验证 `owner_ == self`，Semaphore 醒后验证自己已被 `post()` 从该 semaphore
  队列移除；通用 `Scheduler::unblock()` 只能造成虚假唤醒，不能授予锁或 token。
- `Task::child_next` 与同步原语的 `wait_next` 分离，避免任一次 NVMe wait 覆盖 parent 的 children
  链；`Task::fpu_state` 汇编 ABI 仍由 static_assert 钉在 `+384`。
- NVMe controller 自己以 sleepable submit Mutex 覆盖 SQ 发布到精确 CID completion，新增
  active/ready/completed CID 发布协议；ISR 遇到 unexpected CQE 会继续 drain，且只为当前 active
  request post 一次。CQ phase 后用 acquire fence，SQ doorbell 前用 release fence。
- BSP idle 每次唤醒重查共享 runqueue；reschedule IPI 改为通知所有 other online CPUs，使 AP
  产生的 wake 也能通知 BSP。`net_poll` 的低优先级自选只是没有高优先级 Ready 任务时的结果。
- 删除诊断期的热路径打印与未经证明的 idle skip、signal-return/syscall-return 额外调度 hook。
  IRQ-exit 仍是硬中断 EOI 后的唯一抢占点。

## 验证

- production 临时双 exec / user page-fault 触发器：fresh SMP boot 连续 3/3 出现
  `NVME_REPRO_OK`，零 `CQ CID mismatch` / panic；触发器随后从 production 树移除。
- `run-kernel-test-all`：单核与 SMP 两 leg 均 `942 passed, 0 failed`；AP wake/readback 与 TLB
  shootdown PASS。
- host `test_host`：66/66；独立 `CINUX_HOST_TSAN=ON` 的 `ext2_concurrent` 通过，零 race 报告。
- 编译期 `static_assert(offsetof(Task, fpu_state) == 384)` 与反汇编 `%gs:24` current load 均通过。

## 留续

- async NVMe 仍没有设备无响应超时；应在后续给 blocking wait 加 deadline/cancel，而不是退回 poll。
- 当前 active + CID 是最小 single-inflight 协议，不是多请求 CID→waiter 表；16-bit CID 极端回绕与
  陈旧同 CID CQE 需要 generation 或 request-slot 设计。
- `Task::parent` 仍是裸指针：父任务先 deferred-free、子任务后 exit 时存在既有 UAF 风险。应单独
  做 orphan reparent / parent lifetime 契约，不在 NVMe/抢占根修中临时拼补。
