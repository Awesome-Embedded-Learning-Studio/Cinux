# F3-M2 批3 — 共享资源 refcount 指针化

> 2026-06-18。F3-M2 批3：把进程级共享资源（信号 disposition / cwd / fd 表）从 Task 内联字段重构为引用计数堆对象，为 clone(CLONE_SIGHAND/FILES/FS) 真共享铺路。1 批，794→801（+7 测试），fresh 801/0。批4 clone 消费。

## 背景

Linux 线程（CLONE_*）按 flag 共享进程资源：CLONE_VM 共享地址空间、CLONE_FILES 共享 fd 表、CLONE_SIGHAND 共享信号 disposition、CLONE_FS 共享 cwd。此前 CinuxOS 的 sig_actions 是 Task 内联数组 `SigAction[23]`、cwd 是内联 `char[256]`、fd_table 已是指针但无 refcount——都无法「按 flag 共享」。本批把它们改成 refcount 堆对象（fork 仍 copy 语义；clone 在批4 share）。

## 设计

- **SharedSigActions**（signal.hpp）：`{uint32_t refcount; SigAction actions[kSignalCount];}` + `create/create_copy/acquire/release`。Task 持指针。
- **SharedCwd**（process.hpp）：`{uint32_t refcount; char path[256];}` + 同款 refcount API。Task 持指针。
- **FDTable**（file.hpp/cpp）：加 `refcount_`（ctor=1）+ `acquire/release`（release 到 0 时 close 所有 fd 再 `delete this`）+ `refcount()` 诊断 getter。Task 已持指针，无需改字段。
- **TaskBuilder::build**：新核线程分配默认 SharedSigActions + SharedCwd("/")。
- **fork**：copy 语义——子进程拿自己的私有副本。
- **Task::release_resources**（task_builder.cpp，out-of-line 因 fd_table 的 release 需 FDTable 全定义）：operator delete 时释放三者。

## 关键陷阱

### 1. fork 的 memcpy 拷贝的是「指针」→ 必须立即 detach + create_copy

`std::memcpy(child, parent, sizeof(Task))` 把父的 sig_actions/cwd/fd_table **指针**原样拷进 child——child 与父**别名同一对象**，但 refcount 未 +1（瞬态不一致）。若之后某个 error-path `delete child`，operator delete → release_resources 会 release child 继承来的指针 → **释放父的对象 → UAF**。

修法：memcpy 后**立即**给 child 造私有副本（`create_copy`），且必须**在 stack/addr_space/fd_table 分配之前**——这样所有后续 error-path 的 `delete child` 只释放 child 自己的副本（refcount 1→0），父对象不动。同理 fd_table 先 detach（=nullptr，后续重建）。

### 2. Task 是 slab 分配（无构造函数）+ operator delete 拿 void*

Task 经专用 slab cache（`g_task_cache`）分配，**不调用构造函数**，靠「grow 页零化」+ 手工设字段。所以指针字段声明处的 `{nullptr}` 默认值对 slab 分配**无效**（slab 给的是零化内存，恰巧也是 nullptr，但语义上靠零化不靠默认值）。每个 Task 创建路径（TaskBuilder::build / fork）必须手工分配 sig_actions/cwd。

operator delete 拿到的是 `void*`（对象生命周期已结束但内存未释放）。cast 回 `Task*` 调 `release_resources()` 读字段指针并 release——实操可行（内存完整）。release_resources 定义在 task_builder.cpp（含 file.hpp 全定义），不能内联在 process.hpp（FDTable 仅前向声明）。

### 3. 测试的 `Task t{}` 值初始化 → 指针 nullptr

`Task t{}`（值初始化）把指针零化为 nullptr。F3-M1 的 signal 测试、F2 的 cwd 测试直接戳 `t.sig_actions[...]` / `cur->cwd[...]`——指针化后变野指针解引用。

修法：每个用 Task 跑信号逻辑的测试在声明后 `t.sig_actions = SharedSigActions::create();`；cwd 测试的 mock task `test_task.cwd = SharedCwd::create();`。访问改 `->actions[...]` / `->path[...]`。

### 4. cwd 共享变异语义 + 小 blast radius

CLONE_FS 共享 cwd 时，chdir **原地改** `cwd->path` → 所有 sharer 见（POSIX 正确）；fork copy 则各自独立。path_resolve 早已用 `const char* cwd` 参数解耦，故改的只是 path_util/chdir/getcwd 三处取 `current->cwd->path`，不波及路径解析本体——blast radius 比预想小。

## 验证

- 单测 +7（test_shared_resources）：SharedSigActions refcount/create_copy 独立性/共享变异传播；SharedCwd 默认根/create_copy 独立性/acquire-release；FDTable acquire-release refcount。
- 全量：fresh `run-kernel-test`（`timeout 40`）794→**801/0**，ALL TESTS PASSED。现有 signal/cwd/fork 测试全过（重构未破坏 F3-M1/F2 行为）。
- 实机 GUI 冒烟留批4/批5（clone 真线程路径；批3 改 Task 创建 + cwd/sig，run-kernel-test 已含真机内核 signal/cwd/fork 验证）。

## 下一步

批4 线程组 + clone 核心：Task 加 tgid/group_leader/clear_child_tid/set_child_tid + sys_clone（CLONE_VM/FILES/SIGHAND/FS/THREAD/SETTLS/SETTID/CLEARTID）+ **子进程用户栈返回**（patch syscall.S 帧 user_rsp 槽，GOTCHA#18）。批3 的 refcount 基建在此被 clone 的 share 分支消费（acquire）。
