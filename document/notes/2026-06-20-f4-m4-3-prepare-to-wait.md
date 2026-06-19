# F4-M4 批3（M4-3）:prepare-to-wait 消除 lost-wakeup 窗口

> 2026-06-20 写。接 `2026-06-20-f4-m4-0-1-test-refactor-percpu.md`。顺序 A(用户决策:先 M4-3 后 M4-2 + 不做 timer 抢占)的第一步——单核安全前提下先行修复 lost-wakeup,M4-2 开多核时窗口已闭。分支 `feat/f4-m3-trampoline`,commit `b33264b`。

## 背景(为什么)
M4-2 要把 AP `cli;hlt` 改 `sti;hlt` 让多核并发真实化。届时 Mutex/Semaphore/futex/waitpid 的等待路径会暴露 lost-wakeup:**任务被唤醒后又重新阻塞,永久挂起**。本批在 AP 仍 idle(IF=0)的当前态下先行修复,单核行为不变(绿),为 M4-2 扫雷。

## 侦察:四处同一 lost-wakeup 模式
`Mutex::lock` / `Semaphore::wait` / `futex_wait` / `waitpid` 全是同一形态——
```
等待者(CPU0):            唤醒者(CPU1, 并发):
lock.acquire();
enqueue_waiter(self);    lock.acquire(); waiter=dequeue_waiter();
lock.release();          lock.release(); unblock(self);  // self→Ready 入 runq
// <窗口>
block(self);  // state=Blocked + dequeue(runq) ← 把刚被unblock加进去的自己移除→永久阻塞
```
单核安全:schedule 切走前唤醒者根本没跑(只有一核),窗口内无并发。多核穿透→死锁。waitpid 注释自认「single-core makes check+block atomic」(process_new.cpp:196)。

## 设计:prepare-to-wait(Linux prepare_to_wait 模式)
- **`Scheduler::prepare_to_wait(task)`**:持等待锁(irq_guard)下原子设 `state=Blocked`。current 不在 runq,无需 dequeue。
- **`Scheduler::schedule_blocked()`**:`if (no_reschedule_depth_ == 0) schedule();`——NoRescheduleGuard 感知的 schedule,等待路径在**释放锁后**调。
- **`unblock` 幂等**:仅 `state==Blocked` 才设 Ready+入队,否则 no-op(防 double-enqueue)。
- **`block()` 保留原样**:`test_block_unblock`(block 非 current→Blocked)、`test_block_dispatches`(block current 真切换)直接依赖其语义,不能动;生产等待路径改走 prepare+schedule_blocked,block 退为测试/管理用。

窗口内若被 unblock,self 已 Ready→schedule() 的 next==prev 路径保持运行不睡——不丢唤醒。

## 实现要点
- **嵌套 block 保证 irq_guard 在 schedule_blocked 前析构**:prepare 提前设 state=Blocked,若持锁期间本核 tick→schedule 会因 prev Blocked 切走(持锁!)→ lockdep panic/死锁。解法:`{ auto g = spin_.irq_guard(); ...; prepare_to_wait(self); }` 内层 block 结束 g 先析构(release+sti),**再** `schedule_blocked()`。Mutex/Sem/futex 三处统一此式。
- **irq_guard 替手动 acquire/release**:原 `Mutex::lock` 用 `spin_.acquire`(不关中断);改 irq_guard 关中断,使 prepare(设 Blocked)+enqueue 原子且持锁期间无 tick 切走。短临界区,符合 sync.hpp 注释。
- **waitpid 无等待队列锁**(children 链表 lock-free):本批只把 `block`→`prepare+schedule_blocked`(单核等价 + unblock 幂等缩小窗口),**彻底 SMP 安全(children 锁 + double-check)留 follow-up**——因 waitpid block 路径只在真机跑(测试 kWaitNoHang),且 M4-2 真机验证聚焦 mutex/futex。

## 测试(+2 单测,875/0)
- `test_prepare_to_wait_survives_race_window`(test_sync_concurrent):NoRescheduleGuard 下模拟窗口——`prepare(a)`→a Blocked→`unblock(a)`(并发唤醒)→a 应 Ready 不丢→`schedule_blocked`(guard 下 no-op)→断言 a 仍 Ready。验证 unblock 幂等 + prepare 不冲突。
- `test_unblock_idempotent_on_runnable`:unblock 已 Ready 的 task 是 no-op,不 double-enqueue。
- 现有 mutex/sem/futex 测试兼容:`run_sync_tests`/`run_futex_tests` 的 NoRescheduleGuard 挡 `schedule_blocked`(等价旧挡 block 的 schedule);waiter 由 prepare 设 Blocked。

## GOTCHA 候选(#24)
**prepare-to-wait 的 `schedule_blocked()` 必须在等待锁的 irq_guard 析构之后调用。** prepare 提前设 state=Blocked,若 schedule 在持锁时跑(irq_guard 还活着)→持锁切走→lockdep panic 或死锁(切走的任务无法释放锁)。用嵌套 `{}` 块限定 guard 作用域,确保 `}`(析构:release+sti)先于 `schedule_blocked`。通用铁律:任何「先标记将睡、后 schedule」的模式,标记持锁、schedule 必须释锁后。

## 不变量 / 边界
- 单核行为不变(873→875/0)。
- AP 仍 `cli;hlt` idle(IF=0),本批无并发暴露;修复在 M4-2 `sti;hlt` 后生效。
- prepare 仅设 state(不动 runq);unblock 幂等是 double-enqueue 防线。

## 下一步(M4-2,本会话续做)
- **M4-2-1**:reschedule IPI(vector 0xE0 + stub + no-op handler + add_task/unblock 后 send_ipi 到 idle AP + idle AP 跟踪)。单核 no-op,875/0 不变。
- **M4-2-2**:AP `sti;hlt` idle loop + 首次切换(prev=AP idle task)+ **-smp 2 真机**(AP 跑任务 + 多线程 mutex/futex 不死锁——本批 lost-wakeup 修复的实战检验)。

## 相关文件
- scheduler.hpp/cpp:`prepare_to_wait`、`schedule_blocked`、unblock 幂等、block 注释。
- sync.cpp:`Mutex::lock`、`Semaphore::wait`(irq_guard 嵌套 block)。
- sys_futex.cpp:`futex_wait`。
- process_new.cpp:`waitpid`。
- test_sync_concurrent.cpp:+2 单测。
