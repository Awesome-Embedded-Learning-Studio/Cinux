# F3-M3 批1 — 进程组/会话字段 + fork/clone 继承

> 里程碑 F3-M3(进程组/会话 + waitpid 阻塞),2026-06-19。
> 分支 `feat/f3-m3-process-group`(从干净 main `a287d8b` = F3-M2 PR#17)。
> 批1 commit `77be415`。测试 810 → **815/0**(run-kernel-test)+ host `test_host` 全绿。

## 目标

为 Job Control / TTY 子系统打地基的第一步:给 `Task` 加进程组(pgid)/会话(sid)
身份字段,并让 `fork()`/`clone()` 正确继承。本批**只做字段 + 继承**,
`setpgid`/`getsid`/`killpg` 与 syscall 留批2/3,waitpid 阻塞留批4。

## 决策

**#1 继承规则用方案 A(root fork 自成组),并提取成可测 helper。**
`fork`/`clone` 都是 `new Task` + `memcpy(child, parent, sizeof(Task))`,pgid/sid
本来就被 memcpy 隐式继承了。但隐式依赖 memcpy 不可读、不可测,故把规则集中到
`inherit_process_identity(child, parent, child_pid)`(process_internal.hpp 声明,
process_new.cpp 实现):

- **root fork**(`parent->pgid == 0`,即父是内核/启动 task,无组):子**自成新组 + 新会话**,
  `pgid == sid == child_pid`,`session_leader = child`;
- 否则:子继承父的 `pgid`/`sid`/`session_leader`(同组同会话);
- `controlling_tty` 两种情况都从父继承。

fork/clone 在字段设置区 memcpy 之后显式调用它,覆盖 memcpy 的隐式值 —— 显式 + 单一
职责 + 单测可直接打这个纯函数。

**#2 init(pid=1)自动满足 Linux 语义,无需显式设值。** 关键认知:
`TaskBuilder::build()` 建的内核线程 `pid = 0`(`task_builder.cpp:129`,不经 PidAllocator),
所以 `kernel_init_thread` 是 pid=0,真正的 pid=1 是它 `fork` 出的第一个用户进程。
该 fork 的父(`kernel_init_thread`)pgid=0 → 命中 root 分支 → 子自动 pgid=sid=1,自成
group/session leader。再往下的 fork 命中继承分支,留在组 1。链式测试覆盖此路径。

**#3 `controlling_tty` 只占位。** 默认 `-1`,随 fork 继承;真控制终端 attach 是
F10-M3(TTY)的活,M3 不碰。

**#4 字段放 thread-group 字段(tgid/group_leader)之后。** 进程组(process group,job
control 用)与线程组(thread group,M2 的 tgid)是两个维度,相邻放置便于读者对比。

## 实现

| 文件 | 改动 |
|------|------|
| `kernel/proc/process.hpp` | `Task` 加 `pgid{0}`/`sid{0}`/`session_leader{nullptr}`/`controlling_tty{-1}`(485 行,500 内) |
| `kernel/proc/process_internal.hpp` | 声明 `inherit_process_identity`(include process.hpp 拿 Task) |
| `kernel/proc/process_new.cpp` | 实现(root 自成组 / 否则继承父) |
| `kernel/proc/fork.cpp` | memcpy 后显式调用 |
| `kernel/proc/clone.cpp` | memcpy 后显式调用(CLONE_THREAD 与非 THREAD 都继承调用者的组) |
| `kernel/test/test_process_group.cpp` | 新建,5 测(root 自成组×2 / 继承×2 / kernel_init→init→孙 链式×1) |
| `kernel/test/main_test.cpp` + `CMakeLists.txt` | 注册 `run_process_group_tests()` |

## 验证

- `timeout 40 cmake --build build --target run-kernel-test`:**815 passed, 0 failed**
  (810 + 5 新;clone 全套回归不破)。
- `cmake --build build --target test_host`:全绿(0.45s)。**CI 盲区确认** —— 本批改了
  `Task` 公共字段(加字段,不改语义/mock),host 单测无碍。

## 踩坑 / GOTCHA

- **`process.cpp` 的真名是 `process_new.cpp`**。`inherit_process_identity` 实现要写到
  `kernel/proc/process_new.cpp`(含 `next_tid`/`waitpid`/CoW 的那个),不是 `process.cpp`。
  首次 Edit 用错路径被拒(`File does not exist`),`sed`/`grep` 看文件不算 Read 工具读过,
  Edit 前必须用 Read。
- **memcpy 隐式继承 vs 显式 helper**:memcpy 已把父的 pgid/sid 拷给子,但靠它隐式继承
  不可测、未来改 memcpy 顺序易破。提取 helper 显式化是正解(可测 + fork/clone 共用)。
- IDE diagnostics 的一堆 "not directly included" / "unused header" 警告全是 pre-existing
  (elf_types.hpp/execve.hpp/memory_layout.hpp unused;Atomic/MemoryOrder 经 process.hpp
  间接 include),非本批引入。

## 下一步

批2:`process_group.{hpp,cpp}` —— `setpgid`/`getpgid`/`getsid`/`setsid`(纯字段语义)
+ `killpg`(放 signal.cpp,持 pid registry + `signal_send`,遍历按 pgid 广播)。
