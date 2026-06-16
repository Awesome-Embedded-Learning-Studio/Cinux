# CinuxOS 开发日志（DEVLOG）

> 编年工作日志：**每批 / 每个有意义的迭代一条**（`/done` 自动追加，最新在最上）。
> 像介绍自己这轮工作那样写——**粗略改了什么 + 为什么/决策 + 陷阱/弯路 + 验证结果**。
> 分工：git=机械 diff；PLAN.md=当前状态；OPEN GOTCHAS=活警告；**本文=编年叙事+决策**。条目写定不改（要更正就新写一条）。

**条目模板**（`/done` 按此填，粗略即可、非 diff 复述）：
```
## YYYY-MM-DD <批# / 里程碑 / meta> — <一句话标题> (commit <short>)
- 改动：<哪些文件/区域做了什么，2-4 行>
- 决策/why：<关键取舍、为什么这么做>
- 陷阱/弯路：<gotchas、试错、差点漏的；无则写"无">
- 验证：<测试结果/状态>
```

<!-- 新条目插在此行下方（最新在最上） -->

## 2026-06-16 M2 收尾 — 内核日志/dmesg 完成（ConcurrentRingBuffer + KernelLog + sys_dmesg）
- 改动：批1 [`concurrent_ring_buffer.hpp`](../../kernel/lib/concurrent_ring_buffer.hpp)（MPSC，RingBuffer + `Spinlock::irq_guard`）；批2 [`klog.{hpp,cpp}`](../../kernel/lib/klog.cpp)（LogEntry ring + `klog_*` 宏 + kprintf 攒行 sink）；批3 [`sys_dmesg.cpp`](../../kernel/syscall/sys_dmesg.cpp)（SYS_dmesg=103，手动格式化历史）；批4a `KernelLog::log` 加实时 console 输出（reentrancy guard 避双重）；批4b [`exception_handlers.cpp`](../../kernel/arch/x86_64/exception_handlers.cpp) `[FATAL]`/`[EXCEPTION]`→`klog_error`/`klog_warn`。662→674（+12 测试）。
- 决策/why：dmesg 全链路——Cinux-Base `Logger`/`LogLevel`/`RingBuffer` 就绪，增量是内核持久化（ring sink）+ 用户态读取（sys_dmesg）。`ConcurrentRingBuffer` 是 M1 推迟项落地（MPSC，日志可在 IRQ/panic）。批4a 实时输出是 kprintf→klog_* 迁移前提（否则丢 console 调试）。批4b 高价值优先：崩溃 ring 读不到但 #DB/#BP/#GP-user 非崩溃进 ring；kpanic/dump 保留 kprintf（实时/halt）。
- 陷阱/弯路：`vkprintf_impl` 第三参是 **va_list** 不是可变参数，sys_dmesg 改手动 append helpers；klog_* 实时输出 + klog sink 双重入队用 `g_klog_emit_depth` reentrancy guard 避免；`g_klog_emit_depth` 须定义在 `log()` 前（首版放 anonymous namespace 后部致 undeclared，编译红）。kprintf 全量迁移 294（除 mini 148）未做，留渐进。
- 验证：run-kernel-test 674/0（全程绿）；M2 全为 kernel QEMU 测试，无 host 单测涉及。

## 2026-06-16 M1 收尾 — RingBuffer 消费迁移完成
- 改动：批1（pipe）+ 批2（keyboard）两处 kernel/ 手写环形缓冲统一复用 `cinux::lib::RingBuffer`；ROADMAP F1-M1 ⏳→✅、PLAN 进入待命。
- 决策/why：M1 与 M0 同构——Cinux-Base 既有类型就绪，工作是消费迁移而非造轮子。这是「层化」铁律的兑现：kernel/ 消费 cinux::lib，不在 kernel/ 重写。净减代码、消除两套同款环形缓冲逻辑。
- 陷阱/弯路：迁移前发现 ROADMAP 把 M1 标成「RingBuffer ⏳」误导成待实现，实则 Cinux-Base 早已提供（README 列为 4 大容器之一、文档明写"用于日志和管道"）——里程碑定义应区分「类型就绪」与「内核消费」，已据此修正 ROADMAP/PLAN 描述。
- 验证：全量 run-kernel-test 662/0；批1 另跑 host test_pipe 37/0 + test_sys_pipe 13/0。

## 2026-06-16 批2 — keyboard 事件队列迁移至 cinux::lib::RingBuffer (commit 715a00f)
- 改动：[`keyboard.{hpp,cpp}`](../../kernel/drivers/keyboard/keyboard.cpp) 移除手写 `queue_[64]/head_/tail_`（牺牲槽位策略：head_==tail_ 判空、next==head_ 判满）及 enqueue/poll 逻辑，复用 `RingBuffer<KeyEvent,KEY_QUEUE_SIZE>` 的 `push/pop`；静态存储收敛为 `buf_`，init 重置用 `clear()`。
- 决策/why：与批1 同理。原队列牺牲槽位（容量 63），RingBuffer 用 count_ 不牺牲（容量 64），更贴合 KEY_QUEUE_SIZE 字面；enqueue 满 drop-newest 由 `push` 返 false 实现，InterruptGuard 同步保留。公共接口（init/irq1_handler/poll）签名不变 → sys_read/gui/main 等调用方零波及。
- 陷阱/弯路：IDE 在多 Edit 应用过程中报 queue_/head_/tail_ 未定义 Error——中间 Edit 快照噪音（hpp 已删成员但 cpp 后续 Edit 未反映），全 Edit 应用后消失，编译一次过。test_keyboard 是 QEMU in-kernel 测试（非 host），验证只需 run-kernel-test。
- 验证：run-kernel-test 662/0（test_keyboard 13 项 + dual_path 全过）。

## 2026-06-16 批1 — pipe 内部环形缓冲迁移至 cinux::lib::RingBuffer (commit 0746ebf)
- 改动：[`kernel/ipc/pipe.{hpp,cpp}`](../../kernel/ipc/pipe.cpp) 移除手写 `buffer_[4096]/head_/tail_/count_` 及 write/read/try_read/try_write 的两段回绕拷贝，改用 Cinux-Base `RingBuffer<char,4096>` 的 `push_batch/pop_batch`；私有存储收敛为单个 `buf_`。外层 `Spinlock` + `irq_save/restore` + spin-wait + reader/writer open 标志位原样保留。
- 决策/why：M1 是 M0 同款「消费迁移」——`RingBuffer` 类型 Cinux-Base 早已提供（freestanding header-only，文档明写"用于日志和管道"），内核却手写了一套几乎同款（连用 `count_` 区分满空的策略都一致）。故本里程碑不是造轮子，而是消灭 kernel/ 重复实现，回归「kernel 消费 cinux::lib」层化铁律。只换存储层、不动公共接口 → pipe_ops 与 syscall 边界零影响。
- 陷阱/弯路：RingBuffer 的 `push_batch/pop_batch` 返 `size_t`（新增 size_t 依赖，IDE 报 IWYU，补 `<stddef.h>`）；它非线程安全，靠外层锁保护——正好契合 pipe 既有模型。read 返回的 `writer_open_ ? 0 : 0`（两分支皆 0）是历史形态，保留不"修"。净减 ~109 行。
- 验证：run-kernel-test 662/0；host test_pipe 37/0 + test_sys_pipe 13/0（含 try 往返/partial/满空/EOF/多轮回绕全覆盖）。

## 2026-06-15 批4 — syscall 边界 Error→errno，接回 userspace (commit a81536a)
- 改动：新建 [`kernel/errno.hpp`](../../kernel/errno.hpp)（`cinux::to_errno(Error)` 全 14 变体映射 + `kPascalCase` POSIX errno 常量，freestanding 不引 libc errno.h）；10 个 FS syscall handler（open/read/write/stat/creat/mkdir/unlink/rmdir/getdents/chdir）失败路径由 `return -1` 改为 `return -to_errno(r.error())` 或具体 `-errno`（EFAULT/EBADF/ENOENT/EMFILE/ENOTDIR/ENOTEMPTY…）；28 处受影响测试断言 `== -1` → `< 0`。
- 决策/why：**批3 侦察后并入批4**——proc 无高价值 ErrorOr 目标（execve/waitpid 已是 errno-enum、保真高于 `ErrorOr<void>+Error`，因 `Error` 缺 ENOEXEC/EISDIR/ESRCH；fork 错误全平凡且 ErrorOr 撞 `fork_child_trampoline` 的 rax 锻造；`handle_cow_fault` 是谓词非错误通道）。故 M0 收口落在 syscall 边界：批1/2a/2b 让 FS 返 ErrorOr，批4 把 `Error` 经 `to_errno` 翻成 `-errno` 接回用户态——批1/2a/2b 的投资此前在边界被压成 `-1` 丢弃。
- 陷阱/弯路：找受影响测试断言用了限定变量名的正则 `[a-z_]*`，漏掉带数字的 `r0/r1/r2/r3`（test_syscall/test_shell 的 sys_write 测试），首轮 4 failed；补一记全量 grep + 逐条分类后转绿。与批2b「箭头/点号两形态」同类盲点。
- 验证：662 passed, 0 failed（clang-format 前后各一）。M0（ErrorOr 消费迁移）至此收口。

## 2026-06-15 meta — CODING-TASTE 风格权威 + 接线 (commit 8831b24)
- 改动：新增 `document/ai/CODING-TASTE.md`（扎根 `.clang-format`+真实代码）；`DIRECTIVES` B、`CLAUDE.md`、`AGENTS.md` 加指针；旧 `document/ai_prompts/code_conventions.md` 与本地 `helpers/CodingStyle.md` 转指针。
- 决策/why：注释**全英文**（代码现状如此）；常量/枚举值**迁 k 前缀**（目标态，legacy 迁移中、不批量重写）；clang-format 为机械风格唯一权威。
- 陷阱/弯路：旧 `code_conventions.md`（Milestone-0）与代码脱节约 10 处（C++20、namespace 缩进、指针左右、中文示例…）——正是"乱"的根因，故取代而非沿用。
- 验证：纯文档提交，未跑内核测试。

## 2026-06-15 批2b — read/write/readdir → ErrorOr<int64_t> (commit 6d47c99)
- 改动：ext2_common.cpp / ramdisk.cpp 的 Ext2(Ramdisk)FileOps/DirOps 的 read/write/readdir 改返回 `ErrorOr<int64_t>`；7 个调用方跟进（execve.cpp×3 read、sys_read、sys_write、sys_getdents、sys_rmdir）。
- 决策/why：`value==0` 作 OK（目录读完），`!ok` 作错误——压掉裸 int 三态歧义（正数/0/-1）。
- 陷阱/弯路：侦察只 grep 了箭头形态 `->ops->op(...)`，漏了点号形态 `ops_obj.op(...)`（test_pipe.cpp 局部对象），靠编译暴露；`PipeReadOps`/`PipeWriteOps` 也是 InodeOps 子类差点漏改，适配层须把底层 `-1` 显式翻译成 `Error`。
- 验证：662 passed, 0 failed。

## 2026-06-15 meta — 分层架构 + 7 slash 命令 + Codex 对等 (commit b95f022)
- 改动：新增 `document/ai/`（DIRECTIVES/ROADMAP/PLAN/prompts）+ 仓库根 `CLAUDE.md`/`AGENTS.md` 薄指针 + `.claude/commands/` 7 命令（战术 resume/status/next/done + 战略 roadmap/milestone/audit）；`.gitignore` 放行 `.claude/commands/` 与 `CLAUDE.md`。
- 决策/why：耐久度三层（DIRECTIVES 年 / ROADMAP 里程碑 / PLAN 批）；事实源进仓库让 Claude 与 Codex 共享；commit msg 纯描述、**无 Co-Authored-By**；验证命令固化 `-j$(nproc)`。
- 陷阱/弯路：初版定位"handoff 战术命令"被否——重定向为"Claude 长期主力开发"导向；`.claude/`/`CLAUDE.md` 原被 gitignore，发现后改规则放行（settings.local.json 仍忽略）。
- 验证：命令 bash 片段实测（git log / grep readdir）；纯文档提交。

## 2026-06-15 批1+批2a — VFS/InodeOps 引入 ErrorOr (commit 93e2870)
- 改动：`FileSystem::mount/lookup` → `ErrorOr<void>`/`ErrorOr<Inode*>`；`InodeOps::create/mkdir/unlink/stat` → `ErrorOr`；ramdisk/ext2 实现 + init/execve + ~14 syscall 消费方跟进。（事后补记，细节见 PLAN.md）
- 决策/why：先改 VFS 接口与写/元数据 ops；read/write/readdir 因三态歧义留批2b 单独处理。
- 陷阱/弯路：测试 helper（big_kernel_test.h 的 lookup_or_null 等）把 ErrorOr 降级回 nullptr/0-1 以适配旧断言；`__assert_fail` 须在 crt_stub.cpp 提供以支撑 `<cassert>`。
- 验证：662 passed, 0 failed（两次）。
