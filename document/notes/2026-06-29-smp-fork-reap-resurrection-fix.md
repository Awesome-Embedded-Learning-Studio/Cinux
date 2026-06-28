# 2026-06-29 -smp2 fork exit/reap 复活 saga 修复 — 跨核任务生命周期

## 背景

SMAP 收尾(分支 `feat/f10-tty-dyn`)留了两个 follow-up,其中 #1 是「`-smp2`
ring-3 smoke 偶发假绿:worker 没跑却 CI 绿」。本批干它,结果挖出一个**潜伏的
跨核任务生命周期 saga**——文档 open 的 `f10-shell-launch-smp-race` 真根因。

两个收尾 commit:`ea816dc`(kprintf SMP 串行化,诊断解锁关键)+ `24c4559`
(saga 修复本体)。

## 关键教训(诊断方法论,本批最大价值)

**并发内核日志必须行原子**。CinuxOS `kprintf` 逐字符 `Serial::putc` **全程无锁**,
两个 CPU 并发 kprintf → 字节交织 → 日志全乱(`[SWAYSI...]` 这种),**根本没法看**。
Linux `printk` 拿 `console_lock`/`logbuf_lock` 保证每条消息原子。修:`g_printf_lock`
(Spinlock + `irq_guard`,关中断杜绝同核重入死锁;`kpanic` 只包打印不包 halt 循环
免锁跨无限 halt 卡死别的 CPU)。

**日志可读是诊断的前提**——没有它,后面的复活 saga 连现象都看不清。

## 根因(诊断链,教科书级)

`-smp2` 下 child 在 AP 上 `sys_exit`:设 `state=Zombie` → `yield()`。`schedule()`
本应把它切到 idle。**但它没被切走**,于是 `sys_exit` 返回 → syscall 回用户态 →
child 带垃圾 code 循环 `exit_group(486160)` —— 每个 Zombie child 重入 `sys_exit`
~400 次,且被 reap+free 后还重入(`ALREADY-DEAD`)=「**复活**」UAF。

铁证(`fix_1.log:6487-6489`):
```
6487 sys_exit(0)          from tid=140      # child 干净退出,Zombie,yield
6488 WAITPID reaped pid=10 exit_status=0    # 父 reap+free → Dead+delete
6489 sys_exit(486160) on ALREADY-DEAD tid=140  # free 后又跑 sys_exit = UAF 复活
```

**为什么没被切走**(迭代加仪器定死,非猜):
- `sys_exit` 入口检 Zombie/Dead → 复活在(8085 次)。
- `enqueue` 检「入队非 runnable」= 0、`pick_next` 检「选回非 Ready」= 0
  → 复活**不是**重新入队被选回。
- `schedule()` 入口检 `prev==Zombie`:**ENTER=271~519、SWITCH=22**。271 次 ENTER
  里 `cpu=1 my_idle=0x0` —— **AP 上 `idle()` 返 nullptr**!schedule 队列空 + 无
  idle 可切 → 早返回(`next==nullptr` 且无 idle 兜底)→ Zombie 原地循环。

**AP 为什么没 idle task**:`Scheduler::init()` 把 `idle_tasks_[i]` 全置空后**只重建
BSP 的 `idle_tasks_[0]`**,AP 的 `idle_tasks_[1]` 丢失。smoke bootstrap 调了 `init()`
(`main_test.cpp:657`)→ AP idle 被擦。单核无 AP 不暴露,`-smp2` 才炸。

> 推翻旧主因:之前 `f10-shell-launch-smp-race` 卡在「CoW old_phys 跨核 UAF」,实非
> 主因;真凶是 exit/reap 路径的 **AP idle 丢失 + reap 时序**。CoW old_phys 的跨核
> TLB shootdown 仍是独立 follow-up(见 `handle_cow_fault` 注释)。

## 实现(`24c4559`,4 处)

1. **复活根因 — `init()` 为 online AP 重建 idle**:`smp.hpp` 加 `online_ap_count()`
   getter(`ap_main.cpp` 定义,SEQ_CST 读 `g_aps_online`);`scheduler.cpp init()` 在
   重建 BSP idle 后 `for c in 1..online_ap_count(): setup_ap_idle(c)`。单核测试期 AP
   未 online → no-op;只对 `-smp2` smoke 的 re-init 生效。
2. **reap 时序 — `waitpid` 释放前等 `on_cpu==-1`**(`process_new.cpp`):child 设
   Zombie 发生在 `yield`→context_switch **之前**,父(BSP)扫到 Zombie 就 reap 会
   撞 AP 还在存 child ctx。`context_switch.S:78` 存完 prev ctx 后置 `from->on_cpu=-1`
   ——用它当硬同步点(ACQUIRE load),bounded spin。治 free-during-switch UAF。
3. **`add_task` 加 `wake_ap` 旋钮**(默认 `true`,现有调用不变):bootstrap 用 `false`
   减少 AP 抢初始 worker 的窗口(不彻底,见 4)。
4. **smoke bootstrap park**(`main_test.cpp`):`run_first` 返回 ⟹ 队列空 ⟹ AP 抢走了
   worker(`pick_next` 只取一次;BSP 没拿到就是 AP 拿了)。smoke 正在 AP 上跑会自己
   `outl` 退出,BSP **park(`cli;hlt`)等 AP 的 outl**,不再假退出杀 AP(治 did-not-run
   假绿/假红)。

## 验证

- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿
  (单核 + `-smp2` 各 `ALL TESTS PASSED`),连跑 3 次稳定。
- `-smp2` 多次:`hello 20/20 PASS` + `forktest races=0 PASS`,**零复活、零 #DF**;
  AP 抢 worker 时走「seized by AP; BSP parking」路径同样 PASS。
- 改公共接口(`add_task` 签名)→ `cmake --build build` 全量 + `test_host` host 单测绿。
- 源码无 TEMP 诊断残留(`grep SMP-DIAG` 空)。

## 残留 / 后续

- **CoW old_phys 跨核 TLB shootdown**:`handle_cow_fault` 释放 old_phys 仍是单核正确、
  线程跨核迁移 mid-CoW 有窗口(注释自承)。独立 follow-up,非本 saga。
- **deferred-free vs reap 耦合**:本批用 on_cpu-spin 治 reap 时序(正确但有 spin);
  Linux 风格「task_struct 作 zombie 留到父 wait,内核栈 exit 后即释放」的拆分是更
 优雅的后续重构。
- **原 follow-up #2(exception table 基建)**:仍待做,本 saga 收完后另起。
