# PR #82 CI：scheduler 重初始化与 AP idle 创建竞态根修

> 日期：2026-08-13 · 分支 `feat/nvme-async-io` · 状态：✅ 已验证

## 现象与证据

PR #82 的 11 个 checks 仅 `kernel-tests (Release, lockdep)` 失败。编译、单核 leg 与 SMP
内核测试主体均已通过；SMP leg 在 `950 passed, 0 failed` 后进入 ring-3 `/hello` smoke 时
发生 kernel-mode #PF：

```text
[SCHED] AP1 idle task created tid=156
[SCHED] BSP idle task created tid=157
kernel NULL-pointer dereference @ 0x388
[SCHED] AP1 idle task created tid=158
```

同一 head 的 Release/lockdep ELF 反汇编显示，`ap_main()` 调用
`Scheduler::setup_ap_idle()` 后立即读取返回值的 `Task::irq_stack_top`：

```asm
call Scheduler::setup_ap_idle
mov  0x388(%rax), %rdi
call GDT::tss_set_ist2
```

故障帧 `RAX=0`，而 `Task::irq_stack_top` 恰在 `+0x388`，与日志完整吻合。其他成功矩阵也
出现同一 AP idle 被创建两次；Release + lockdep 组合命中了时序窗口，但 lockdep 本身不是根因，
也没有报告锁序错误。

底层 `init()` / `setup_ap_idle()` 无同步竞态早于本 PR 已存在；PR commit `5e03483` 新增的
`ap_idle->irq_stack_top` 直接解引用，使这次命中以精确的 `nullptr + 0x388` #PF 暴露出来。

## 根因

测试套件会多次调用 `Scheduler::init()` 角色扮演任务，结束时 `initialized_` 已是 true。
旧 smoke 顺序先由 `run_smp_ap_wake_test()` 启动 AP，随后 BSP 才再次 `Scheduler::init()`：

1. AP selfcheck 返回 true，因旧 `initialized_ == true` 而跳过等待；
2. AP 在 `setup_ap_idle(1)` 创建并打印 tid 156；
3. BSP 的最终 `Scheduler::init()` 清空整个 `idle_tasks_[]`；
4. AP 从全局槽位重新读取返回值，得到 nullptr；
5. `ap_main()` 解引用 `ap_idle->irq_stack_top`，触发 `nullptr + 0x388`；
6. BSP 随后又为同一槽创建 tid 158。

这不是只影响诊断的空指针：即使让 `setup_ap_idle()` 返回局部变量，AP 的 `current` 仍会指向
被调度器表遗忘的旧 idle，BSP 则保存新 idle，后续 fallback/context switch 会继续破坏状态。

## 修复

- 在 smoke 配置下，把最后一次 `Scheduler::init()` 前移到 `run_smp_ap_wake_test()` 顶部，
  且位于单核提前返回之前；单核仍获得干净 run queue，SMP 则保证 destructive reset 完成后才
  `boot_aps()`。
- 删除 AP 已可能在线后的旧 `Scheduler::init()`，明确禁止“AP 正在调度时重初始化”。
- 将 `initialized_` 从普通 `bool` 改为 `Atomic<bool>`：初始化末尾 release-store，AP 等待与 IRQ
  路径 acquire-load。初始化顺序修复负责代际生命周期；atomic 负责把首次初始化的 class、
  runqueue 与 idle table 写入正式发布给 AP，两者职责不混淆。
- 同步 `online_ap_count()` 契约注释，避免以后重新引入在线 AP destructive re-init。

## 验证结果

- 仓库规定默认 gate `cmake --build build --target run-kernel-test -j$(nproc)`：953 passed，
  0 failed。
- CI 同构 Release + lockdep + GUI + musl smoke 的 `run-kernel-test-all`：单核与 SMP 两 leg
  均 core 950 passed，0 failed，`/hello` 均 20/20 PASS；SMP 的 AP wake/readback PASS。
- 同配置 SMP leg 额外连续 3 轮：每轮 core 950 passed，0 failed；AP wake/readback 与
  `/hello` 20/20 PASS；无 NULL dereference、panic 或 `TESTS FAILED`。
- 每轮 `AP1 idle task created` 均恰好 1 次。该额外判据排除了旧实现“退出码为 0 但重复构造”
  的假绿。
