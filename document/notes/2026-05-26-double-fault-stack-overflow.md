# 2026-05-26 Double Fault — GUI Shell 启动时内核栈溢出

## 现象

进入 GUI 后点击 Shell 图标拉起 shell 时概率性触发 Double Fault (#DF, vector 8)：

```
[GUI] Shell child jumping to user mode: entry=0x0000000000400250
==== EXCEPTION: #DF (vector 8) ====
  RIP   = 0xFFFFFFFF8101A881   RSP   = 0xFFFF800009214FD0
  ERROR CODE = 0x0000000000000000
[FATAL] Double Fault -- halting.
```

非必现，概率取决于 PIT 定时器是否在关键时间窗口触发。

## 根因

**内核栈溢出**。`create_shell_terminal()` 调用 `fork()`，子进程继承父进程 (gui_worker) 的栈帧后继续在同一条深调用链上执行：

1. `fork()` 将父进程栈内容**整体拷贝**到子进程 16KB 栈上
2. 子进程在 fork 返回后执行：`cli → new AddressSpace() → new FDTable() → execve("/bin/sh") → 用户栈映射 → activate → jump_to_usermode()`
3. 其中 `execve()` 内部的 `clear_user_mappings()` 有 4 层嵌套页表遍历循环，是栈消耗大户
4. 以上调用链加上 PIT 中断嵌套（~376 字节），总栈消耗超过 16KB
5. 写入 guard page → Page Fault → Page Fault handler 在同一溢出栈上再次 Page Fault → Double Fault

**间歇性原因**：`context_switch.S` 恢复子进程上下文后 `sti` 到 `gui_init.cpp` 的 `cli` 之间有一个极短时间窗口。PIT 恰好在此窗口触发就会多压 ~376 字节，将本已紧绷的栈推入 guard page。

### 栈布局佐证

| 分配 | 任务 | Guard Page | 栈范围 | 栈顶 |
|------|------|-----------|--------|------|
| #3 | gui_worker | 920F000-9210000 | 9210000-9214000 | 9214000 |
| **#4** | **shell 子进程** | **9214000-9215000** | **9215000-9219000** | **9219000** |

崩溃 RSP = `0xFFFF800009214FD0`，精确落在 #4 的 guard page (9214000-9215000) 中。RCX = `0xC0000102` (MSR_KERNEL_GS_BASE) 表明崩溃发生在调度器切换上下文的 `rdmsr`/`wrmsr` 附近。

### 关键发现

子进程路径**不使用任何从 fork 继承的状态**：新建 AddressSpace、新建 FDTable、execve 替换一切。fork() 拷贝父栈是纯粹的开销和隐患。

## 修复方案

用 `TaskBuilder` 替代 `fork()` 创建 shell 子进程。子进程从干净 16KB 栈开始执行专用入口函数 `shell_child_entry()`，栈深度从零起步。

### 改动

| 文件 | 改动 |
|------|------|
| `kernel/proc/process.hpp` | Task 加 `void* private_data` 字段（传递管道 inode） |
| `kernel/gui/gui_init.cpp` | 新增 `ShellLaunchInfo` + `shell_child_entry()`，用 TaskBuilder 创建子进程 + 6 行 PID/parent/scheduler 内联设置 |

`fork.cpp`、`context_switch.S`、`scheduler.cpp`、`execve.cpp` 均未改动。

### 修复前后对比

| | 修复前 (fork) | 修复后 (TaskBuilder) |
|---|---|---|
| 子进程栈起点 | 拷贝父帧（~数 KB 已用） | 干净栈，16KB 全部可用 |
| 栈初始深度 | 继承父调用链深度 | 从入口函数开始，深度 = 0 |
| PIT 中断 376B | 可能推入 guard page | 栈余量充足，不受影响 |
| CoW 页表拷贝 | 执行后被子进程丢弃 | 不执行 |
| FDTable 拷贝 | 执行后被子进程丢弃 | 不执行 |

## 验证

- 编译通过，662 项单元测试全部通过
- `make run` 进入 GUI 反复点击 Shell 图标，Double Fault 不再复现
