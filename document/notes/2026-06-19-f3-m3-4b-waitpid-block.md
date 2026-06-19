# F3-M3 批4b — waitpid 阻塞 + WNOHANG + exit 唤醒 parent

> 里程碑 F3-M3,2026-06-19。分支 `feat/f3-m3-process-group`。
> 批4b commit `734d6a1`(批4a `ee13cac`)。测试 827/0 回归 + host 绿 + 实机 GUI 到桌面。

## 目标

waitpid 默认**阻塞**等子进程 exit(闭环进程管理,shell/CFBox 不再忙等烧 CPU),
`WNOHANG` 非阻塞,exit 唤醒 waiting parent。建立在批4a 的 Zombie 契约上。

## 决策

**#1 waitpid 加 options + 重扫 loop。** proc::waitpid(pid, status, **options**, alloc):
- 找 Zombie → reap(Ok)
- 有 child 未 exit + `kWaitNoHang` → NotExited(非阻塞)
- 有 child 未 exit + 默认 → `Scheduler::block(parent)` + loop 重扫(醒后子已 Zombie)

**#2 关中断 + 单核串行化规避 lost wakeup(同 futex handshake)。** CinuxOS 单核(F4 SMP 前):
parent 设 `waiting_for_child=true` 后调 `block()`(内 schedule 切走),在此期间没有别的
task 能运行(单核 + 协作)。child 的 sys_exit 只能在 parent 已切走后运行 → 看到
`waiting_for_child=true` → `unblock(parent)`。所以 check+set+block 天然原子,无需显式锁。
(多核后需重审,见 futex 同款。)

**#3 sys_exit 唤醒 waiting parent。** 批4a 已让 exit 进 Zombie + dequeue;批4b 加:若
`task->parent->waiting_for_child` 则 `Scheduler::unblock(parent)`。parent 醒来 loop 重扫,
此时 child 已 Zombie → reap。Task 加 `bool waiting_for_child{false}` 字段。

**#4 terminal 析构 + 所有 test 改 kWaitNoHang(R1 关键)。** terminal.cpp 析构原是
非阻塞轮询(spin 1000 次)。改默认阻塞后,析构 waitpid 必须传 WNOHANG,否则 shell 还活
着时析构挂死 → GUI 卡。同理 test_fork_exec 的 7 个 waitpid 调用全改 kWaitNoHang —— 其中
**test_waitpid_not_exited(child Running)若不改会直接挂死**(R1 实锤)。

## 实现

| 文件 | 改动 |
|------|------|
| [process.hpp](kernel/proc/process.hpp) | `kWaitNoHang=1` 常量 + waitpid 签名加 options + Task `waiting_for_child` 字段 |
| [process_new.cpp](kernel/proc/process_new.cpp) | waitpid 重写:重扫 loop + Zombie reap + WNOHANG + block |
| [sys_exit.cpp](kernel/syscall/sys_exit.cpp) | exit 唤醒 waiting_for_child 的 parent(unblock) |
| [sys_waitpid.cpp](kernel/syscall/sys_waitpid.cpp) | 取第3参 options 传 proc::waitpid |
| [terminal.cpp](kernel/gui/terminal.cpp) | 析构 waitpid 传 kWaitNoHang |
| [test_fork_exec.cpp](kernel/test/test_fork_exec.cpp) | 7 个 waitpid 调用全改 kWaitNoHang |

## 验证

- `timeout 40` run-kernel-test:**827/0**(waitpid 签名变,所有调用点 4 参同步;test 改
  kWaitNoHang 后 test_waitpid_not_exited 验 WNOHANG+Running→NotExited 不挂死)。
- `cmake --build build --target test_host`:全绿(0.38s)。
- `timeout 40 cmake --build build --target run`:**GUI 启动到桌面**(Desktop/gui_worker,
  无 panic/halt)。timeout 杀(GUI 持续)。

**诚实记录验证深度**:
- **WNOHANG 路径**:run-kernel-test 覆盖(test_waitpid_not_exited/zombie/not_found 等改
  kWaitNoHang 后即测非阻塞路径)。
- **阻塞 + 唤醒 + reap 闭环**(parent block → child exit unblock → reap):**实机未直接
  触发** —— 需 shell 启动后 wait child,而 timeout40 内 shell 未交互启动。靠 ① 逻辑
  正确性(关中断+单核 lost-wakeup 规避,对齐已验证的 futex handshake);② 批4a 的
  exit Zombie 契约;③ 留未来真 shell 程序(或 F3-M4 调度循环测试)端到端验证。
- block()→schedule() 切走,**单测无法测阻塞路径**(无 scheduler loop 挂死,同 futex 坑)。

## 踩坑 / GOTCHA

- **R1 实锤**:test_waitpid_not_exited(child Running + 默认 waitpid)批4b 后必挂死。
  审计 grep 抓到,改 kWaitNoHang 解。**改 waitpid 默认阻塞前,必须 grep 全部调用点确保
  WNOHANG 或 zombie 就绪。**
- IDE diagnostics 的 "too few arguments"(test_fork_exec 853)是 Edit replace_all 的
  滞后快照,实际已 4 参(编译 827/0 证实)—— 别被 IDE 中间快照误导,编译是真理。

## 下一步

批5:**收尾**。文档(PLAN/ROADMAP/todo/notes/GOTCHA)+ 全量验证(run-kernel-test +
test_host + make run)+ F3-M3 总结。F3-M3 全 5 批(批1-3 进程组/会话 + 批4a/4b waitpid
阻塞)收官。
