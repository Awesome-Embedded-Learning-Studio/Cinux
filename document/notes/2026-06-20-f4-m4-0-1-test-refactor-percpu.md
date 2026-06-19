# F4-M4 批 0+1:phantom-task 测试重构 + current_ per-CPU 化

> 2026-06-20 写。接 `2026-06-19-f4-m4-handoff.md`:把交接里的 M4-0(测试重构)+ M4-1(current_→percpu)落地。两批两 commit,均绿。M4-2/3(让 AP 真跑任务 + lost-wakeup)高风险,留新会话。
> 分支 `feat/f4-m3-trampoline`,本会话新增 commit:`ac92bef`(M4-0)+ `7dba770`(M4-1)。

## 背景(为什么)
F4-M3 收官后 AP `-smp 2` online 但 `cli;hlt` 永久 idle。M4 让 AP 真跑任务,第一块是把 `Scheduler::current_`(静态全局,Phase 1 留的 P0#1)改成 per-CPU —— M4-2(IPI 唤醒 idle AP、AP 中断 handler 碰 current)的前提。

上一轮试做 M4-1 后**回退**:production 正常但**测试内核 hang**。根因:phantom-task 测试(test_sync/futex/clone)**故意只写 `percpu()->current`、不碰静态 `current_`**,靠两者分歧让 `block(task)` 比对 `current_` 判 false → 不 schedule(测试线程当「幻影 CPU」观察状态)。M4-1 把 block 改读 percpu → 判 true → schedule → runq 空 → 切 idle(`while hlt`)→ 测试线程再也回不来。

**核心判断:真正的 hack 是那条「未文档化的 `current_`/`percpu` 分歧」,不是 role-play 本身。** role-play(测试线程当单一确定性时间线、切换「谁在当 current」)是干净简单的测试技术,M4-3 lost-wakeup 也能用它做精确交错测试,不是过渡品。认真权衡过「真实协作调度重写 11 个测试」(更写实但 ~3× 工作量、ctx 引导/任务生命周期/tid 污染风险高),否定 —— phantom 测试测的是等待队列/状态机,不是调度切换;切换路径由真机 boot + test_scheduler 覆盖。**结论:把 role-play 显式化,而非推翻它。**

## M4-0:phantom-task 测试显式化(commit `ac92bef`)
1. **`Scheduler::NoRescheduleGuard`**(scheduler.{hpp,cpp}):RAII 计数器 `no_reschedule_depth_`(默认 0)。`block()` 改为 `if (task == current_ && no_reschedule_depth_ == 0) schedule();` —— 守卫内 block 仍把 task 置 Blocked + 出队(状态机/等待队列语义照常),只是不上下文切换,让 harness 线程继续观察。文档化:表达「in-kernel 测试 harness 单线程 role-play、无调度循环」,生产 depth==0 无影响。**仅 gate block() 内部那次 schedule,tick/yield/exit_current 不碰。**
2. **phantom 测试转 `set_current` + 守卫**:test_sync/futex/clone 里凡 `percpu()->current = X` → `Scheduler::set_current(X)`(~30 处,sed 机械替换)。因为 `no_reschedule_depth_` 是普通静态计数器(非 per-thread),在 `run_sync_tests`/`run_futex_tests` 入口各放**一个** section 守卫即覆盖整段(test_clone cleartid 是孤立 phantom,单独包)。set_current 让 `current_`/percpu 一致(GOTCHA#21 既约定),彻底消分歧。
3. **真 dispatch 测试** `test_block_dispatches_to_runnable`(test_scheduler):a=set_current(add b 到 runq),`block(a)` **不走守卫** → 真 schedule → b 跑 → b unblock(a) + 返回落进 exit_current → 切回 a。证明 block(current) 真能切到就绪任务(守卫不掩盖切换 bug)。

### M4-0 踩坑
- **`RoundRobin::clear()` + `init()` 清 runq(测试隔离)**:新 dispatch 测试是第一个真跑 `block(current)→schedule→pick_next` 的,暴露 `init()` 不清 runq —— 之前测试 `add_task`+`unblock` 留的 stale task(block_test/init_test)还 Ready 在 `default_rr_` 里,pick_next 选中 stale task 去真跑它的 entry(本不该跑)→ 混乱 → NotNull nullptr 断言。现有测试没暴露是因为它们都 block **非 current** 任务(不 schedule)。修:加 `RoundRobin::clear()`,`init()` 调之。boot 时 runq 空 no-op,生产无影响;无现有测试依赖跨测试 stale task。
- **忘设 `g_block_dispatch_a = a`**:dispatch 测试首版只把静态指针初始化成 nullptr,没在 build a 后赋值 → b 跑 `unblock(nullptr)` → NotNull nullptr 断言。addr2line 5 帧回溯(block_dispatch_b_entry → NotNull ctor → kpanic)秒定位。

## M4-1:current_ 静态 → per-CPU(commit `7dba770`)
仅 scheduler.{hpp,cpp}:删 `static Task* current_;` 成员;`current()`→`return percpu()->current;`;`set_current`/schedule/exit_current/run_first/tick/yield/block 全走 per-CPU current,删旧双写(`current_=X; percpu()->current=X` → 只留 percpu)。**~35 个 `Scheduler::current()` 调用点全经 accessor,零改动** —— 这是低风险的依据。

### M4-1 关键正确性(GOTCHA 候选)
**`fxrstor current()->fpu_state` 在任务恢复点必须读 percpu,绝不能用局部 `next->fpu_state`。** `context_switch(&prev->ctx,&next->ctx)` 返回时跑在 next 的上下文,但执行的是 **prev 当初被切出时**那条 `fxrstor` 指令(prev 的栈帧里 `next` 还是 prev 当年的 next,已过期)。旧代码用全局 `current_`(被「切到本任务那次」schedule 设成本任务)读对;M4-1 必须用 `current()`(读 percpu,语义等价旧全局),**不能**用局部 `next`(过期栈帧值,会恢复错任务的 FPU)。replace_all 把 3 处 fxrstor 一并改对。

### 验证(M4-1)
- run-kernel-test **873/0**(phantom 靠 M4-0 守卫+set_current 不 hang)。
- run-kernel-test-smp **873/0**(-smp 2,AP online idle 不干扰)。
- test_host **48/0**(删公共成员,push 前 CI 盲区补全量)。
- run-smp:`[AP1] online (apic_id=1)` + `[SMP] 1 AP(s) online` + GUI Desktop 渲染,BSP 续跑稳定,无 panic/fault。

## 不变量 / 边界
- 单核 `-smp 1` 行为全程不变(percpu[0] 等价旧 current_)。
- AP 仍 `cli;hlt` 永久 idle(IF=0,中断路径不碰 percpu current 调度)→ 本批无并发暴露。真正并发暴露在 M4-2 改 `sti;hlt`。
- 守卫仅 gate block();NoRescheduleGuard 是 test 基建,M4-3 的 prepare-to-wait 分解 block() 时自然被取代/吸收。

## 下一步(M4-2/3,留新会话)
- **M4-2**:AP `cli;hlt`→`sti;hlt` + IPI 唤醒(add_task/unblock 后 send_ipi 到 idle AP)+ 共享 runq(AP pull 任务)。AP 真跑任务,但暴露 lost-wakeup → 须配 M4-3。
- **M4-3**:lost-wakeup prepare-to-wait(mutex/semaphore/futex/waitpid 的 check+enqueue 持锁原子;block() 改条件阻塞)。最高风险,可用本批的 role-play 守卫做精确交错测试。

## 相关文件
- 调度器:`kernel/proc/scheduler.hpp`、`scheduler.cpp`(NoRescheduleGuard、RoundRobin::clear、current_→percpu)。
- phantom 测试:`kernel/test/test_sync.cpp`、`test_futex.cpp`、`test_clone.cpp`;真 dispatch 测试:`test_scheduler.cpp`(test_block_dispatches_to_runnable)。
- percpu 基建:`kernel/proc/percpu.hpp`。
- 设计稿:`~/.claude/plans/warm-plotting-giraffe.md`(本会话 plan)。
