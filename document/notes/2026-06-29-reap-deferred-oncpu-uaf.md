# reap_deferred on_cpu 同步 — exit/reap 栈 UAF #DF 修复

> 2026-06-29。用户 `target run` 偶现 #DF(kernel_init exit 后,RIP 跑飞到 VMM::unmap,RSP 栈页 not-present)。根因:`reap_deferred` 缺 `on_cpu` 同步,free 了还在用的内核栈。

## 根因

`exit_current()`(scheduler.cpp)把 prev task `enqueue_deferred` 到 per-CPU deferred list(line 283)在 `context_switch`(line 306)**之前**。这个窗口里 prev 还在自己内核栈上跑(`on_cpu == cpu_id`)。

`reap_deferred`(scheduler.cpp:249)快照 deferred list 后直接 `free_kernel_stack` + `delete`,**没 check `on_cpu`**。若 tick IRQ 在 enqueue→context_switch 窗口抢占触发 `schedule → reap_deferred`,会 free 掉 prev 还在用的栈 → use-after-free → #DF(RIP 跑飞到 `VMM::unmap` 区 + 栈页 not-present,因为栈已经被回收/demand-page 不了)。

PR#44(复活 UAF saga)给 **waitpid reap** 加了 `on_cpu==-1` 同步,但 **deferred-free reap 路径漏了**。

## 修

`reap_deferred` 加 `on_cpu==-1` check(ACQUIRE load):没切走的(`on_cpu != -1`)放回 deferred list,等 `context_switch.S`(line 78)存完 ctx 设 `on_cpu=-1` 后,下次 reap 再 free。对齐 PR#44 waitpid reap 的 on_cpu 同步做法。

## 验证

- `run-kernel-test-all` 两 leg **967/0** + AP readback PASS(fix 不破坏 scheduler,deferred 仍正常回收)。
- #DF 在生产 GUI boot 路径(`kernel_init_thread` exit),test kernel 不复现(不同代码路径);偶现 #DF 100% 修绝需生产 boot 压力(用户本机 `target run` 多次确认)。

## GOTCHA

- per-CPU deferred list 防的是**跨 CPU** reap,不防**本 CPU tick 抢占**(enqueue→context_switch 窗口);`on_cpu` check 是硬同步点(`context_switch.S` 存完设 -1)。
- 偶现 race bug 难写确定性测试(时序);机制验证靠 `on_cpu` 语义 + 逻辑审 + 不回归。

## 牵连

PR#44(复活 UAF saga)同族;F4-followup(迁移竞态 on_cpu)基建。追加 `feat/f10-m3-tty`(用户拍板同分支续叠,不开新分支避免分支竞态)。
