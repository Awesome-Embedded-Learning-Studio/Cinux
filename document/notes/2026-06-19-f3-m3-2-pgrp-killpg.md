# F3-M3 批2 — 进程组身份操作 setpgid/setsid + killpg 广播

> 里程碑 F3-M3,2026-06-19。分支 `feat/f3-m3-process-group`。
> 批2 commit `824449c`(批1 `77be415`)。测试 815 → **824/0**(run-kernel-test)+ host `test_host` 全绿。

## 目标

进程组/会话的**身份操作 + 信号广播**:`setpgid`/`getpgid`/`getsid`/`setsid`
(纯字段) + `killpg`(遍历 pid registry 按 pgid 广播),并**闭环 sys_kill 的
pid<0 分支**(之前是 `TODO(F3-M3)` 返 EINVAL)。

## 决策

**#1 killpg 归 signal.cpp,身份操作归 process_group.cpp。** 按 POSIX 头文件归属
切分:`killpg` 在 `<signal.h>`(它是信号广播,要遍历 pid registry —— 而 registry
在 signal.cpp 匿名 namespace),`setpgid`/`setsid` 在 `<unistd.h>`(进程身份,纯
字段操作)。process_group.cpp 因此不碰 scheduler/registry,纯函数易测。

**#2 setpgid MVP 放宽(单用户/root 模型)。** 省略 Linux 的 EACCES(改已 exec 的
task)、EPERM(跨 session 建新组)、"只能改自己或子进程"。只校验 `pgid >= 0` 和
`pgid == 0 ⇒ 用自身 pid`(POSIX:pgid 0 = 自建组)。这些限制是权限/语义完整性,真
多用户/TTY 时再加(F10)。

**#3 setsid 检查"已是 group leader"→ EPERM。** `task->pgid == task->pid` 即已是
leader(Linux:leader 不能再 setsid)。其余:pgid=sid=pid,session_leader=self,
controlling_tty=-1(新会话无控制终端,真 attach 留 F10)。

**#4 killpg 实现细节:**
- `pgid == 0` 解析为调用者自己的组(`Scheduler::current()->pgid`,POSIX 语义);
- 遍历 `g_registry_head`,pgid 匹配者调 `signal_send`;
- **迭代安全**:接受者可能在遍历中退出 —— `signal_send` 容忍 Zombie/Dead
  target(返 ESRCH 不崩),所以无需额外锁;
- 返回接收者数,0 ⇒ 无此组(syscall 层翻 -ESRCH)。

**#5 sys_kill pid<0 接 killpg。** `kill(-pgid, sig)` 调 `killpg(-p, s)`,sent==0 返
-ESRCH,否则 0。闭环 [sys_signal.cpp:42](kernel/syscall/sys_signal.cpp#L42) 的
`TODO(F3-M3)`。

## 实现

| 文件 | 改动 |
|------|------|
| `kernel/proc/process_group.{hpp,cpp}`(新) | setpgid/getpgid/getsid/setsid(55 行) |
| `kernel/proc/signal.hpp` | `killpg` 声明(delivery 区) |
| `kernel/proc/signal.cpp` | `killpg` 实现(遍历 registry,~24 行) |
| `kernel/syscall/sys_signal.cpp` | sys_kill pid<0 → killpg(闭环 TODO) |
| `kernel/proc/CMakeLists.txt` | 加 process_group.cpp |
| `kernel/test/test_process_group.cpp` | 批2:3 setpgid + 1 getpgid/getsid + 3 setsid + 2 killpg = 9 测 |

## 验证

- `timeout 40` run-kernel-test:**824 passed, 0 failed**(815+9;clone 全套回归)。
- `cmake --build build --target test_host`:全绿(0.44s)。批2 加了 signal.hpp 公共
  声明(killpg)+ 新文件,host 单测无碍。

## 踩坑 / GOTCHA

- **漏 include process_group.hpp 被 IDE 编译前抓到。** test 用 `setpgid` 但首次只
  include 了 signal.hpp/process.hpp,没 include 声明所在的 process_group.hpp →
  IDE diagnostics 报 "No member named 'setpgid'"。补 include 即解。**编译前 IDE
  diagnostics 抓未声明符号,比等编译省一轮往返。**
- **killpg 测试清理必须放断言前(GOTCHA#19 家族)。** 注册测试 task 到全局 registry
  后,若 `TEST_ASSERT` 失败早 return,会跳过 unregister → 残留测试 task 污染后续
  测试的 registry 遍历。修法:先做完所有操作 + 记录结果到本地变量,再 unregister +
  release,最后才断言变量。断言失败时 registry 已清理干净。
- **errno 用硬编码数字 + 注释**(项目惯例,sys_kill 等同款):-3 ESRCH / -22 EINVAL /
  -1 EPERM。errno.hpp 存在但 translation boundary 普遍硬编码。

## 下一步

批3:4 个 syscall —— `sys_setpgid`(109)/`sys_getpgid`(121)/`sys_getsid`(124)/
`sys_setsid`(112),注册 syscall 表 + libc wrapper(setpgid/setsid 用 2 参 handler,
getpgid/getsid 用 1 参)+ 单测(syscall_dispatch 端到端)。
