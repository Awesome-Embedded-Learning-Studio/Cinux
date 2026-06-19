# F3-M3 批3 — 进程组/会话 4 syscall + libc wrapper

> 里程碑 F3-M3,2026-06-19。分支 `feat/f3-m3-process-group`。
> 批3 commit `b228f67`。测试 824 → **827/0**(run-kernel-test)+ host `test_host` 全绿。

## 目标

把批2 的库函数接成 syscall:`sys_setpgid`(109)/`sys_setsid`(112)/`sys_getpgid`(121)/
`sys_getsid`(124),注册 syscall 表 + libc wrapper,用户态可调。

## 决策

**#1 handler 用 resolve_target 统一 pid 解析。** POSIX 的 pid==0 = 调用者自己。
抽 `resolve_target(pid)`:pid==0 或 pid==caller->pid 返 current,否则
`signal_find_task_by_pid(pid)`。4 个 handler 共用(setsid 无参,直接 current)。

**#2 errno 硬编码 + 注释**(项目惯例,sys_kill/sys_waitpid 同款):-3 ESRCH。
proc 层(setpgid/setsid)返的值(>=0 / -errno)直接透传为 syscall 返回值。

**#3 libc wrapper 复用现有 `_syscall1/2`** + `SyscallNr` cast,与 sys_kill/sys_futex 同款。
setsid 无参,用 `_syscall1(SYS_setsid, 0)`(无 _syscall0)。

## 实现

| 文件 | 改动 |
|------|------|
| `kernel/syscall/sys_pgrp.{hpp,cpp}`(新) | 4 handler + resolve_target(60 行) |
| `kernel/syscall/syscall_nums.hpp` | SyscallNr 加 setpgid=109/setsid=112/getpgid=121/getsid=124 |
| `kernel/arch/x86_64/syscall.cpp` | include + 4 个 `syscall_register` |
| `kernel/syscall/CMakeLists.txt` | 加 sys_pgrp.cpp |
| `user/libc/syscall.{h,cpp}` | 4 wrapper 声明 + 实现 |
| `kernel/test/test_process_group.cpp` | 批3:3 端到端测(setpgid+getpgid / setsid+getsid / ESRCH) |

## 验证

- `timeout 40` run-kernel-test:**827 passed, 0 failed**(824+3)。
- `cmake --build build --target test_host`:全绿(0.39s)。

## 踩坑 / GOTCHA(重要)

**#21 `Scheduler::current()` 读 static `current_`,不是 `g_per_cpu.current`。**

首验 **825 passed, 2 failed** —— 批3 的 sys_setpgid/setsid 两个测试 fail。根因:
测试用 `g_per_cpu.current = &t` 装 current,但 `Scheduler::current()`(scheduler.cpp:234)
返静态 `current_`,**不是** `g_per_cpu.current`。两者只由 `Scheduler::set_current(task)`
(scheduler.cpp:238)同步设置。只设 g_per_cpu.current → current_ 仍 nullptr →
`resolve_target(0)` 返 nullptr → handler 返 ESRCH → 断言 fail。

**修**:测试改用 `Scheduler::set_current(&t)`(设 current_ + g_per_cpu.current 两者),
cleanup 用 `Scheduler::set_current(nullptr)`。改后 827/0。

**通用铁律**(写进 PLAN OPEN GOTCHA #21):单测装 current **必须用
`Scheduler::set_current()`**,勿直接 `g_per_cpu.current = &t`。任何经
`Scheduler::current()` 的 handler(setpgid/setsid/未来的 waitpid 阻塞等)都受此约束。
test_clone 的 futex 侥幸(futex 内部不经 Scheduler::current()),掩盖了这层。

## 下一步

批4:**waitpid 阻塞**(高风险 R1)。默认 block、`WNOHANG` 不阻塞、exit 唤醒 waiting parent。
**第一步必须 grep 全部 waitpid 调用点审计**——默认行为从 non-blocking 变 blocking,
现有 `test_fork_exec` 等若没传 WNOHANG 且无 zombie 会挂死(GOTCHA#19 家族 + futex block
挂死坑)。复用 `Scheduler::block/unblock`(futex 同款)+ sys_exit 唤醒 parent。
