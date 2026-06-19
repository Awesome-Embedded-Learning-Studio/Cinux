# M3: 进程组/会话 ✅ 完成 (2026-06-19, F3-M3)

> **完成**：5 批 810→827/0。进程组/会话（pgid/sid/session_leader/controlling_tty）+ setpgid/setsid/getpgid/getsid/killpg + 4 syscall + libc wrapper + fork/clone 继承 + waitpid 阻塞（WNOHANG + exit 唤醒 parent）+ exit Dead→Zombie 契约修正。关键 GOTCHA：#21（Scheduler::current 读 static current_，测试设 current 用 set_current）。详见 PLAN「✅ F3-M3」段 + `document/notes/2026-06-19-f3-m3-*.md`。

> 为 Job Control 和 TTY 子系统提供基础。
> Task 增加 pgid/sid 字段，实现 setpgid/getsid 等系统调用。

## 目标

实现进程组（process group）和会话（session）概念：
- 进程组：用于信号广播（如 SIGINT 发给整个前台组）
- 会话：用于控制终端管理

## 现有代码

- `kernel/proc/process.hpp` — Task 结构体
- `kernel/proc/pid.hpp` — PID 分配器（1..256）

## 任务清单

### T1: Task 进程组字段

**文件**: `kernel/proc/process.hpp`

```cpp
struct Task {
    // ... existing ...
    int    pgid;          // 进程组 ID
    int    sid;           // 会话 ID
    Task*  session_leader;// 会话 leader（sid == pid 的进程）
    int    controlling_tty; // 控制终端 fd（-1 = 无）
};
```

- [x] 添加 pgid / sid / session_leader / controlling_tty 字段
- [x] init 进程：pgid=0, sid=0

### T2: 进程组管理函数

**文件**: `kernel/proc/process_group.hpp`, `kernel/proc/process_group.cpp`

```cpp
namespace cinux::proc {

// 设置进程组
int setpgid(Task* task, int pgid);

// 获取进程组 ID
int getpgid(Task* task);

// 获取会话 ID
int getsid(Task* task);

// 向进程组发送信号
int killpg(int pgid, Signal sig);

// 获取进程组中所有进程
// (遍历 scheduler 的 task 列表，筛选 pgid 匹配的)

} // namespace cinux::proc
```

- [x] setpgid() — 设置/创建进程组
- [x] getpgid() — 查询
- [x] getsid() — 查询会话
- [x] killpg() — 向整个进程组发送信号

### T3: 进程组相关 Syscall

**文件**: `kernel/syscall/sys_pgrp.cpp`

| Syscall | 编号 | 说明 |
|---------|------|------|
| sys_setpgid | 109 | 设置进程组 |
| sys_getpgid | 121 | 获取进程组 ID |
| sys_getsid | 124 | 获取会话 ID |
| sys_setsid | 112 | 创建新会话 |

- [x] sys_setpgid(pid, pgid)
- [x] sys_getpgid(pid)
- [x] sys_getsid(pid)
- [x] sys_setsid() — 创建新会话 + 新进程组，调用者成为 leader
- [x] 注册到 syscall 表

### T4: fork/clone 继承

**文件**: `kernel/proc/process.cpp`

- [x] fork 继承父进程的 pgid 和 sid
- [x] clone(CLONE_THREAD) 不改变 pgid/sid

### T5: 单元测试

- [x] setpgid 创建新进程组
- [x] setsid 创建新会话
- [x] fork 继承 pgid/sid
- [x] killpg 向组内所有进程发信号

## 产出物

- [x] `kernel/proc/process_group.hpp` / `.cpp`
- [x] `kernel/syscall/sys_pgrp.cpp`
- [x] Task pgid/sid 字段
- [x] fork 继承
- [x] 单元测试
