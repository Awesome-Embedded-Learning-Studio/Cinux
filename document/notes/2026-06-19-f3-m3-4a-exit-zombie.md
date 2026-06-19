# F3-M3 批4a — sys_exit 进 Zombie + scheduler 跳 Zombie(契约修正)

> 里程碑 F3-M3,2026-06-19。分支 `feat/f3-m3-process-group`。
> 批4a commit `ee13cac`。测试 827/0 回归 + host 绿 + 实机 GUI 到桌面。

## 背景:批4 审计发现 waitpid 阻塞的隐藏依赖链

propose 批4 是"waitpid 默认阻塞",但审计发现正确性依赖 3 个现有契约问题:

1. **sys_exit 设 `Dead` 不是 `Zombie`**(sys_exit.cpp:45),但 waitpid 查 Zombie ——
   生产 exit 的 child 是 Dead,**waitpid 永远 reap 不到真 child**(测试靠手动设 Zombie
   绕过,F3-M1 留的局限)。阻塞会等一个永不出现的 Zombie → 挂死。
2. **scheduler.cpp:275 只跳 Blocked/Dead**,不跳 Zombie —— 若 exit 改 Zombie,
   死 task 会被调度。
3. **terminal.cpp:54 析构 waitpid 是非阻塞轮询**(spin 1000 次,不传 WNOHANG)——
   改默认阻塞会让析构卡在第一次 waitpid → GUI 挂死。

故批4 拆 **批4a(契约修正)** + **批4b(阻塞)**,契约先行。本笔记是批4a。

## 关键发现:pick_next 不查 state

`RoundRobin::pick_next()`(scheduler.cpp:53-67)是纯 round-robin:**取 queue 头,
强制设 Running(63),放回尾** —— **不查 state**。所以一个 Zombie task 若留在 run queue,
会被 pick_next 设成 Running 并切换 → 执行死 task → 崩。

`exit_current`(181-186)为此 **dequeue**(185)把 Dead task 移出 queue。但 `sys_exit`
原本**不 dequeue**(只设 Dead + yield),靠 schedule(275)拦截 Dead(低效但不崩)。
改成 Zombie 后这条路不通(Zombie 会被 pick_next 设 Running),所以 sys_exit 必须
也 dequeue。

## 改动

| 文件 | 改动 |
|------|------|
| [sys_exit.cpp](kernel/syscall/sys_exit.cpp) | `state = Dead` → `Zombie` + `sched_class->dequeue(task)`(对齐 exit_current,移出 queue 等 reap) |
| [scheduler.cpp:275](kernel/proc/scheduler.cpp#L275) | schedule 防御跳 Zombie(`!= Blocked && != Dead && != Zombie`),双保险防 pick_next 选到 |

Zombie task 出 queue 后留在内存等 reap;waitpid 走 parent 的 **children 链**
(process_new.cpp:159)找到它,不依赖 run queue。reap 翻 Dead(process_new.cpp:195)。

## 验证

- `timeout 40` run-kernel-test:**827/0**(scheduler 改动不破现有)。
- `cmake --build build --target test_host`:全绿(0.41s)。
- `timeout 40 cmake --build build --target run`:**GUI 启动到桌面**(Desktop icons:
  Shell, Calculator;gui_worker 运行;kernel_init exit_current 退出不炸)。timeout
  杀(GUI 持续运行)。

**诚实记录验证深度**:实机冒烟里 `kernel_init` 走 **exit_current**(非 sys_exit,
我没改),所以 sys_exit 的 Zombie+dequeue 路径**未在冒烟中直接触发**(shell 需交互
启动才 exit)。其正确性靠:① 逻辑对齐已验证的 exit_current dequeue 模式;② 批4b
的 waitpid reap 闭环会触发 sys_exit(shell exit → Zombie → terminal reap)验证。
scheduler(275)跳 Zombie 经所有 schedule 调用覆盖(实机启动全程 schedule)。

## 下一步

批4b:**waitpid 阻塞**。proc::waitpid 加 options/WNOHANG(默认 block、WNOHANG 返 0)
+ exit 唤醒 waiting parent(unblock)+ **terminal 析构改传 WNOHANG**(防析构挂死,
关键 R1)+ 单测(WNOHANG 路径 + exit 唤醒,避真 block 挂死——block 调 schedule 切走,
单测无 scheduler loop 会挂死,同 futex 坑)。复用 Scheduler::block/unblock。
