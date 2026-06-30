# DEBT-022: ProcFS /proc TOCTOU use-after-free — 锁内 snapshot 修复

> 2026-06-30 · `fix/procfs-toctou-snapshot` · 接 5 弧并发波次 post-hoc 审核

## 背景

5 弧并发波次(ProcFS/ext2/TCP/HPET-RTC/Pipe-FIFO,PR#51-55)合入 main 后做 post-hoc
审核(5 agent 逐弧读真码)。4 弧 clean,**ProcFS 挖出 1 个 HIGH:真实 UAF**。

`/proc/<pid>/{stat,cmdline}` 的 read 路径调 `signal_find_task_by_pid(pid)` 拿 `Task*`,
该函数在 `g_registry_lock` 下找到指针后**释放锁返回**;`format_proc_stat` 随后在锁外
解引用 `t->pid/name/state/ppid/tgid/uid/gid` 共 7 个字段。

`signal.cpp` 与 F6-M2 note 的注释都写「tasks 永不释放,安全(DEBT-002 未修)」——但
**这个前提已失效**:DEBT-002 在 F-QA Q4e-3(`4bb6ca4`/`e6ce2f4`)已修,Task 现经
`exit_current → signal_unregister_task → reap_deferred → delete t` 真释放。

故 SMP(-smp 2)下:
- CPU0 `cat /proc/<pid>/stat` → find_by_pid 拿 `t` → unlock;
- CPU1 该 task `exit_current` → reap_deferred `delete t`;
- CPU0 `format_proc_stat(t,…)` 解引用已 free 的 Task → **UAF**。

`/proc` 经 sys_open/sys_read 可达,这两条 syscall 不关抢占/IRQ——窗口完全敞开。同族
[[reap-deferred-oncpu-uaf]] / [[smp-migration-context-race]]。

## 目标

关闭 UAF,**不动 registry 接口**(register/unregister/find_by_pid 不变),仍在 F6 范畴。

## 设计

对齐 `killpg`(signal.cpp:201-217)的「锁内 snapshot」范式——但 killpg snapshot 的是
指针(仍依赖"永不释放"旧假设);ProcFS 要更稳,**snapshot 值**:

1. **`TaskSnapshot` POD**(`process.hpp`,TaskState 之后):pid/state/ppid/tgid/uid/gid +
   `name[kTaskNameMax=16]`。name 是**字节缓冲**(拷字节,不拷指针)——snapshot 完全自
   包含,锁释放后(及未来 name 存储方式变化时)都不悬。
2. **`signal_snapshot_task(pid, TaskSnapshot&)`**(`signal.hpp/cpp`):`g_registry_lock`
   下找到 task,把字段拷进 snapshot(name 逐字节拷、截断到 `kTaskNameMax-1`,对齐 Linux
   `TASK_COMM_LEN`)。返 bool(找到)。
3. **ProcFS read 改走 snapshot**:`ProcStatFileOps::read`/`ProcCmdlineFileOps::read`
   不再 `find_by_pid` 拿裸指针,改 `TaskSnapshot snap; signal_snapshot_task(pid, snap)`。
   `format_proc_stat`/`format_proc_cmdline` 签名从 `const Task*` 改 `const TaskSnapshot&`。
4. **订正过时注释**:`signal_find_task_by_pid` 的注释从「safe while never freed」改为
   WARNING——指针仅在持锁期有效,锁外解引用是 UAF;field 读取须走 `signal_snapshot_task`。
   保留 find_by_pid(其它调用方:test_clone/test_fork_exec/sys_pgrp/ProcFS lookup 活性
   门——只判 nullptr 或容忍 Zombie/Dead)。

## 决策

- **name 拷字节而非指针**:`Task::name` 是 `const char*`「static storage, not owned」
  (全字面量),指针其实不悬——但本次修复的教训正是"别信赖过时的不释放契约",故拷字节
  防御未来 name 改堆存。代价仅 16 字节栈。
- **`kTaskNameMax=16`**:对齐 Linux comm(15+NUL)。现有 kernel 任务名均 ≤15,无输出回
  归;超长名截断是 Linux 语义,非缺陷。
- **不动 registry 接口**:长期正解是 Task refcount(RCU-safe registry),但那是 DEBT-002
  级别的大改;snapshot 是 F6 范畴内、零接口变更的收敛修。

## 验证

- 全量编译绿(改公共头 process.hpp/signal.hpp 触发大重建,所有消费者过)。
- `run-kernel-test-all` 两 leg:**1020 passed, 0 failed**(单核)+ ALL TESTS PASSED
  (-smp 2 AP readback)。新增 `test_snapshot_task_copies_fields`(1019→1020);既有
  stat/cmdline 读测自动验 snapshot 端到端。
- host `ctest`:**69/69** 全过(public 头未破 mock)。

## 陷阱

- **clangd 滞后误报**:改 procfs_content.hpp 签名(Task*→TaskSnapshot&)后,IDE 报
  procfs.cpp "no matching function"——是 clangd 拿旧 preamble 的误报,build 真实。
- **find_by_pid 还有其它调用方**:不可直接删/改签名;只订正注释 + 加 snapshot 替代。
  sys_pgrp 返裸指针给调用方——潜在同族债(未修,范围外)。
- 残余同族窗口:killpg 仍 snapshot 指针(靠 signal_send 容忍 Zombie/Dead);registry
  TOCTOU 全闭要等 refcount/RCU(长期)。

## 文件

- `kernel/proc/process.hpp` — `kTaskNameMax` + `TaskSnapshot`
- `kernel/proc/signal.{hpp,cpp}` — `signal_snapshot_task` + find_by_pid 注释订正
- `kernel/fs/procfs_content.{hpp,cpp}` — format 改吃 `const TaskSnapshot&`
- `kernel/fs/procfs.cpp` — stat/cmdline read 走 snapshot
- `kernel/test/test_procfs.cpp` — `test_snapshot_task_copies_fields`
