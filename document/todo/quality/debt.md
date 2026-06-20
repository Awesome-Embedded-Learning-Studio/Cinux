# CinuxOS — 代码质量债务登记表（Code Quality Debt Registry）

> **持续迭代的技术债登记**。审计发现 → 登记在此 → **不急着修**，按优先级排期，分批闭环。
> 跨 Feature 域，单一事实源。每条给稳定 ID（`DEBT-NNN`），便于后续引用 / 排期 / 闭环。
>
> **与 OPEN GOTCHAS 互补**（见 `PLAN.md`）：GOTCHA = 已踩过的坑（事后教训）；DEBT = 审计前瞻发现的待修债（事前清单）。两者都该读。
>
> **审计触发**：2026-06-20 用户要求「以 Linux 严肃程度系统性整理代码质量，抓出 TODO 泛滥 / workaround 堆积 / 内存偶现挂死 / 四处崩溃，方便持续迭代」。方法：多维度逐个审计真实代码（每维度读代码取证，grep 坐实），登记而非立即修复。
>
> **流程入口**：每轮提交门禁见 `document/ai/QUALITY-GATES.md`；深度审计方法见 `document/todo/quality/audit-guide.md`；每轮报告见 `document/todo/quality/reports/`；粘贴式命令见 `document/ai/prompts.md` 的 `/preflight`、`/quality-review`、`/infra-audit`、`/fix-debt`。

## 如何用此表

- **状态机**：🆕 登记待办 → 📅 已排期（归入某里程碑/批）→ 🔧 修复中 → ✅ 已闭环（注明 commit + 验证）
- **优先级**：P0 偶现崩溃/挂死（直击痛点） / P1 数据损坏·慢性泄漏 / P2 加固·一致性 / P3 边角·防御
- **闭环纪律**：修复一条时，移到对应里程碑 PLAN 段落 + 写 `document/notes/` 笔记，此处状态改 ✅ 并留 commit 指针；**不删条目**（保留审计轨迹）。
- **新增**：后续每审一个维度，把发现按本表格式追加到对应严重性段。

---

## 审计维度计划（12 维度）

> 用户要求「记录打算从哪些维度排查」。权威方法见 `audit-guide.md`。每维度：读真实代码取证 → grep 坐实 → 写一次性 report → 登记高价值发现。

| # | 维度 | 状态 | 备注 |
|---|------|------|------|
| D1 | 架构不变量 | ⏳ 待审 | DIRECTIVES/ErrorOr/Cinux-Base/层化 |
| D2 | 内存生命周期（悬垂/UAF/buffer/所有权） | ✅ 已审 2026-06-20 | 见 `reports/2026-06-20-memory-smp-audit.md` |
| D3 | SMP / 并发安全（F4 多核后） | ✅ 已审 2026-06-20 | 见 `reports/2026-06-20-memory-smp-audit.md` |
| D4 | 进程 / 线程生命周期 | ⏳ 待审 | fork/clone/exec/exit/wait/signal 状态机 |
| D5 | 调度 / 迁移 / CPU 上下文 | ⏳ 待审 | ctx switch / GS / TLS / FPU / runqueue |
| D6 | 用户 / 内核边界 | ⏳ 待审 | user pointer / VMA 权限 / syscall ABI / signal frame |
| D7 | 错误处理 / 崩溃韧性 | ⏳ 待审 | panic/OOM/栈/递归/诊断 |
| D8 | 测试覆盖盲区 | ⏳ 待审 | user-mode PF / SMP 迁移 / 设备路径 |
| D9 | 静态 / 动态检查工具 | ⏳ 待审 | clang-tidy/UBSAN/lockdep/mini-KASAN/kmemleak |
| D10 | 文档 / 可追溯性 | ⏳ 待审 | TODO/workaround/GOTCHA/notes/PLAN 同步 |
| D11 | 模块组织 / 可维护性 | ⏳ 待审 | 500 行软上限/重复实现/头依赖 |
| D12 | 发布 / 回归 / 变更管理 | ⏳ 待审 | 一批一 commit 一验证/回滚点/残余风险 |

**进度**：2/12 已审。剩余 10 维度按用户「每批 2 个、慢慢来」节奏推进。

---

## 根因归纳（最重要的洞察）

两份报告（内存安全 + 并发 SMP）**独立交叉印证**，用户痛点「内存偶现挂死、四处崩溃」的燃料集中在**两个互为因果的系统性洼地**：

1. **进程/线程生命周期没闭环** —— 退出不释放（DEBT-002）+ CoW 无 mapcount（DEBT-003）+ CLONE_VM 无 refcount（DEBT-006）。本质是缺「物理页/地址空间/任务」三者的**引用计数与退出清理基础设施**（Linux 用 `_mapcount` / `mm_struct` refcount / `do_exit→exit_mm` 全套解决）。
2. **F4 漏网的全局可变状态** —— registry（DEBT-001）/ PidAllocator（DEBT-005）/ waiting_for_child（DEBT-004）。都在 F4 关注的调度器主线之外，单核严格串行永不显现，多核偶现炸。

> 当前 `-smp 2`「干净」很可能是 AP 仍主要跑 idle、并发压力没压到这些路径；多进程 + kill + fork 频繁交织时，DEBT-001/003/004 必现。这三条合起来几乎可确定是「偶现挂死/崩溃」的头号嫌疑。

**建议收敛方向**（待用户拍板，不在本表执行）：上述洼地天然适合收敛成一个「进程生命周期与引用计数」里程碑（exit cleanup + CoW mapcount + AddressSpace refcount + registry/PidAllocator 加锁 + waiting_for_child 原子化），拆批走 PR。

---

## 🔴 Critical

### DEBT-001 `g_registry_head` 全局任务注册表完全无锁 → 跨核并发崩溃
- **维度**: 并发/SMP　**优先级**: P0　**状态**: 🆕 登记待办　**核验**: ✅ grep 坐实
- **位置**: `kernel/proc/signal.cpp:36,47-77,153-175`
- **现象**: 裸全局单链表 `Task* g_registry_head`，`signal_register_task`(add_task 调) / `signal_unregister_task`(exit 调) / `signal_find_task_by_pid`(sys_kill 调) / `killpg`(sys_kill pid<0 调) 全部直接操作链表，**周围零锁匹配**。killpg 注释自称「no extra locking is needed」——该判断在多核下错误。
- **根因**: F4 未触碰 signal.cpp 注册表；单核严格串行永不触发，多核 fork/exec/kill 交织 → 头插写与遍历读并发 → 读半链接指针 / 悬垂 `registry_next` → 跳飞或 UAF（Task 被 slab 复用后 signal_send 读别对象数据）。
- **修复建议**: 给 `g_registry_head` 加全局 irq-safe `Spinlock`（add_task 可能 IF=0）。遍历持锁；`signal_send`→terminate 不能持锁，先收集 pid 列表再释锁发送。
- **关联 GOTCHA**: 无（OPEN GOTCHAS 未记录注册表并发）

---

## 🟠 High

### DEBT-002 退出任务的 TCB / 核栈 / 地址空间永不释放（系统性泄漏）
- **维度**: 内存安全　**优先级**: P1　**状态**: 🆕 登记待办（GOTCHA#11 已登记但**等级被低估**）　**核验**: ✅
- **位置**: `kernel/syscall/sys_exit.cpp:34-74` / `kernel/proc/scheduler.cpp:210-240`(exit_current) / `kernel/proc/process_new.cpp:120-221`(waitpid reap)
- **现象**: `sys_exit` 只置 Zombie + dequeue + yield；`exit_current` 标 Dead + context_switch 切走；waitpid reap 标 Dead + free pid + 解链 —— **三者都不 delete Task / free 核栈 / delete addr_space**。`release_resources` 只在 operator delete 内调，而 operator delete 只在 fork/clone error-path 触发，正常退出从不跑。
- **根因**: 缺 task exit cleanup。每个退出进程泄漏 Task(1008B slab)+4 页核栈+整棵 PML4 子树页表。长时间跑 shell 逐步耗尽 KMEM_SLAB 与物理页。更是 DEBT-003/006 的放大器（不做它，引用计数无从谈起）。
- **修复建议**: waitpid reap 中 `delete target`（→release_resources 释放 sig_actions/cwd/fd_table）+ 释放核栈（`g_vmm.unmap`+`g_pmm.free_pages`，注意 direct-map 不 unmap，GOTCHA#7）+ `delete addr_space`（析构 free_subtree）。CLONE_THREAD 共享 addr_space 需 refcount，最后线程才释放（联动 DEBT-006）。
- **关联 GOTCHA**: #11（exit_current leak，待 task exit cleanup）

### DEBT-003 CoW 物理页无引用计数 → fork+exec use-after-free
- **维度**: 内存安全　**优先级**: P0　**状态**: 🆕 登记待办　**核验**: ✅ grep 坐实（`grep mapcount` 零结果）
- **位置**: `kernel/proc/fork.cpp:49-93` / `kernel/proc/process_new.cpp:70-114`(handle_cow_fault) / `kernel/proc/execve.cpp:62-111`(clear_user_mappings)
- **现象**: fork `copy_page_table_level` 把可写 PTE 标 `FLAG_COW` 并共享**同一物理页**（`dst_table[i].raw = src_table[i].raw`，物理地址不变），**物理页无任何 refcount**。`clear_user_mappings`(execve) 叶子层**无条件** `free_page` 不检查 FLAG_COW/共享。
- **根因**: 经典 fork+exec：子 execve → free 掉与父 CoW 共享的物理页 → 父进程 PTE 仍指向已释放页 → PMM 重分配 → 父读垃圾/踩坏别人。**确凿的「正常用法触发 UAF」**。根因 = CoW 缺物理页引用计数（Linux `_mapcount`）。
- **修复建议**: 引入物理页 `_mapcount`（buddy order_ 数组旁加 int16）。fork CoW 共享页 `_mapcount++`；clear_user_mappings free 前每页 `_mapcount--`，归 0 才真 free；CoW fault 复制后旧页 `--`、新页 `=1`。F3 CoW 共享内存里程碑核心前置。
- **关联 GOTCHA**: 无（PLAN 列 CoW 未做但未标 UAF 风险）

### DEBT-004 `waiting_for_child` 普通 bool 跨核非原子 → lost-wakeup 偶现挂死
- **维度**: 并发/SMP　**优先级**: P0　**状态**: 🆕 登记待办　**核验**: ✅ grep 坐实
- **位置**: `kernel/proc/process.hpp:259`(`bool waiting_for_child`) / `kernel/proc/process_new.cpp:215,218`(父 CPU 写) / `kernel/syscall/sys_exit.cpp:57`(子 CPU 读)
- **现象**: `waiting_for_child` 普通 bool。父 CPU waitpid 写 true/false，子 CPU exit 读 `task->parent->waiting_for_child` 决定是否 `unblock(parent)` —— 跨 CPU 无 atomic/无内存屏障。
- **根因**: 子 CPU 读到 stale 值（父刚置 true 但子还见 false）→ 不唤醒 → 父永睡。**头号偶现挂死嫌疑**。F4-M4 prepare-to-wait 修了 waitpid 自身 check-block 窗口，但漏了子 exit 侧对 parent 标志的非原子读。
- **修复建议**: 最干净 —— sys_exit **无条件** `unblock(parent)`（unblock 已幂等，仅 Blocked 态入队），去掉 `waiting_for_child` 门控，彻底消除 stale 读窗口。或改 `lib::Atomic<bool>` + Acquire/Release。
- **关联 GOTCHA**: 无（F4-M5 注释自称已分析 children 无需锁，但漏了此标志位的跨核可见性）

### DEBT-005 `PidAllocator` 无锁 → 双核分配相同 pid
- **维度**: 并发/SMP　**优先级**: P1　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验（agent 报告，未亲验）
- **位置**: `kernel/proc/pid.cpp:14,20-59`；调用点 `sys_waitpid`(process_new.cpp:163) / fork/clone
- **现象**: 全局单例 `g_pid_alloc` 无内嵌锁，`alloc` 的 check-then-set（`if(!in_use_[candidate]) in_use_[candidate]=true`）非原子。
- **根因**: 两核同时 fork → 同一 candidate 都见 `!in_use_` → 都置 true → **两 task 拿相同 pid** → 注册表键冲突 / find_task_by_pid 返错 / waitpid 认错子。
- **修复建议**: `PidAllocator` 内置 irq-safe `Spinlock`，alloc/free/is_allocated 持锁；或 atomic bitmap + CAS。
- **关联 GOTCHA**: 无

### DEBT-015 syscall handler 栈帧过大（char[PATH_MAX] 缓冲置栈，4-8KB/16KB 栈）
- **维度**: 内存安全(栈)　**优先级**: P1　**状态**: 🆕 登记待办(F-QA Q1-1 触发)　**核验**: ✅ -Wframe-larger-than=1024 坐实
- **位置**: `kernel/syscall/sys_creat.cpp:29,44`(8272B) / `sys_mkdir.cpp:74`(8256) / `sys_unlink.cpp:74`(8240) / `sys_rmdir.cpp:103`(8288) / `sys_open.cpp:72`(4144) / `sys_chdir.cpp:78`(4144) / `sys_stat.cpp:75`(4224) / `sys_dmesg.cpp:109`(4400) / `kernel/fs/path.cpp:88`(4096)
- **现象**: 9 个 syscall/path handler 在 16KB 核栈上放 `char resolved[PATH_MAX]`(4096) + 常第二个 `char parent_buf[PATH_MAX]` → 单帧 4-8KB。`-Wframe-larger-than=1024` 全部命中。
- **根因**: path 解析缓冲放栈上(对齐 POSIX PATH_MAX)；sys_creat 尤甚(两个 PATH_MAX=8KB)。16KB 栈下 syscall 上下文 + 中断嵌套 + 调用链(lookup/create)有溢出风险。Linux 用 `getname`/`struct filename` 堆分配 path。
- **修复建议**: path 缓冲改堆(`kmalloc(PATH_MAX)` + RAII 释放,复用 F2-M7b slab)或专用 per-call path 缓冲设施;目标单帧 <1024B。修后启用 `-Wframe-larger-than=1024 -Werror=frame-larger-than` 入门禁。
- **验证建议**: 修后 `-Wframe-larger-than=1024` 零命中 → 升 `-Werror=frame-larger-than`;`timeout 40 run-kernel-test` 绿(碰 syscall 路径)。
- **关联 GOTCHA**: 无

---

## 🟡 Medium

### DEBT-006 CLONE_VM 共享地址空间无引用计数 → 线程退出损坏共享页表
- **维度**: 内存安全　**优先级**: P2　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/clone.cpp:266-271`（注释自承 `// shared (no refcount yet)`）/ `kernel/proc/process.hpp:176`
- **现象**: clone CLONE_VM 时 `child->addr_space = parent->addr_space` 无 refcount。配合 DEBT-002（退出不 delete）目前侥幸不出事。
- **根因**: 一旦加 exit cleanup，共享 addr_space 的线程退出若 `delete addr_space` → 兄弟线程 PML4 整棵 free → 全员崩；反之不释放则最后线程退出时永久泄漏。多线程程序「偶现崩溃」高度可疑于此。
- **修复建议**: `AddressSpace` 加原子 refcount；clone CLONE_VM 时 acquire；线程退出 release，归 0 才 delete。与 DEBT-002 同批。
- **关联 GOTCHA**: 无

### DEBT-007 `quantum_remaining_` 单一共享 quantum → 多核时间片错乱
- **维度**: 并发/SMP　**优先级**: P2　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/roundrobin.cpp:40,109,142-156`
- **现象**: `default_rr_` 全局单例的 `quantum_remaining_` 被 `lock_.irq_guard()` 保护（不崩溃），但两核 tick 各自递减同一变量。
- **根因**: 实际时间片变 `DEFAULT_TIME_SLICE / ncpus`，一核耗尽 recharge 影响另一核正在跑的任务。**行为错非崩溃**，调度不可预测。
- **修复建议**: quantum 改 per-task（`Task::quantum_remaining`）或 per-CPU，对齐 Linux `task_struct->rt.time_slice`。
- **关联 GOTCHA**: 无

### DEBT-008 signal_setup_frame 写信号帧不校验栈 VMA → 二次 segfault
- **维度**: 内存安全　**优先级**: P2　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/signal.cpp:265-315`
- **现象**: `R = user_rsp - pad - 8 - sizeof(SignalFrame)`(~160B) 后写信号帧/trampoline，**不查 R 是否落合法 Stack VMA / 是否越 guard page**。
- **根因**: 深递归栈近底时收信号 → R 落 guard page 或 VMA 外 → 触发 PF → 投 SIGSEGV，但此刻信号帧写一半、栈已损坏、原信号正投递中 → 语义混乱的「偶现挂死」。Linux 投递前 expand_stack / 查 altstack。
- **修复建议**: 投递前校验 `[R, user_rsp)` 落 Stack VMA 内；否则改用 sig_altstack 或直接 SIGSEGV 默认终止。
- **关联 GOTCHA**: #11（PF 硬门控+栈增长，未覆盖信号帧写入）/ #16（sigreturn trampoline，未覆盖）

### DEBT-009 clear_user_mappings 不识别 huge page entry → 误当 PT 页释放
- **维度**: 内存安全　**优先级**: P2　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/execve.cpp:81-105`
- **现象**: 4 层遍历叶子层 `free_page`，中间层也 `free_page`，**全程不检查 `entry.huge`（PS bit）**。
- **根因**: 用户空间若引入 2MB/1GB huge（mmap/brk 未来可能），huge entry 基址被当 PDPT 表页 free，并向下把 huge 内容当 PT 解析 → free garbage 物理页 → PMM 状态错乱。当前潜伏。
- **修复建议**: 每层先判 `entry.huge`：huge entry 直接 `free_page(phys_addr)` 并清零，不向下走。`AddressSpace::~AddressSpace` 的 free_subtree 同样需补。
- **关联 GOTCHA**: #13（huge split 破坏 direct-map，相关但针对 VMM.map）

### DEBT-010 `FDTable` refcount 用 `guard()` 非 `irq_guard`，与 R3 不一致
- **维度**: 并发/SMP　**优先级**: P2　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/fs/file.cpp:29-54`
- **现象**: `FDTable::acquire/release` 用 `lock_.guard()`（非 IRQ-safe），对照 SharedCwd/SharedSigActions（F4-M5 R3）已改 `__atomic_*_fetch(ACQ_REL)`。
- **根因**: 当前 IRQ 路径不碰 FDTable，不立刻死锁。但属「未爆但脆」的同步原语选型不一致 —— 未来任何 IRQ handler 触达 FDTable 即本核持锁重入死锁。
- **修复建议**: 统一 `irq_guard()`，或像 SharedCwd 把 refcount 改 atomic（单字段独立于 fds_[]，release 到 0 再持锁清理）。
- **关联 GOTCHA**: 无（R3 范围明确只覆盖 SharedCwd+SharedSigActions）

---

## 🟢 Low

### DEBT-011 slab 双重释放检测为启发式（word[1]==poison），可伪造
- **维度**: 内存安全　**优先级**: P3　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/mm/slab.cpp:242-246`
- **现象**: `free_locked` 用 `words[1]==kSlabPoison` 判双重释放。对象第二字段恰好等于 poison → 误报静默泄漏；两次 free 间被重分配改写 → 漏检 → freelist 环化 → 后续 alloc 返同指针 → UAF。
- **修复建议**: 改 slab 级 per-slot 状态位（SlabHeader bitmap 标 inuse/free），或 free 时查 slot 是否已在 freelist。当前启发式标注「非权威检测」可接受。Linux SLUB 用 redzone + freelist 指针校验。
- **关联 GOTCHA**: 无

### DEBT-012 execve phnum 无上限校验 → 损坏 ELF 触发 3.6MB 分配
- **维度**: 内存安全　**优先级**: P3　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/elf_types.cpp:60` / `kernel/proc/execve.cpp:189-194`
- **现象**: `validate_elf_header` 只查 `e_phnum==0`，无上限。`new Elf64_Phdr[phnum]` 最大 65535×56≈3.6MB。
- **修复建议**: 加 `if(ehdr->e_phnum > 256) return BadElfHeaders;`（典型 <20）。
- **关联 GOTCHA**: 无

### DEBT-013 sys_pipe 写用户 int[2] 前仅校验规范地址，未校验映射存在
- **维度**: 内存安全　**优先级**: P3　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/syscall/sys_pipe.cpp:102-104,35-49`
- **现象**: `is_user_addr` 只查规范地址规则，不验证已映射；直接解引用写。
- **修复建议**: 用 `copy_to_user` 风格封装，失败返 -EFAULT。当前 demand-paging 硬门控兜底（设计已知），可接受。
- **关联 GOTCHA**: 无

### DEBT-014 `no_reschedule_depth_` 静态全局非原子（生产恒 0）
- **维度**: 并发/SMP　**优先级**: P3　**状态**: 🆕 登记待办　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/scheduler.cpp:45,52-57,389,434` / `scheduler.hpp:221`
- **现象**: `static int no_reschedule_depth_` 多核共享，所有写点在 kernel/test/，生产恒 0。
- **根因**: 生产只读共享无竞争；但语义是 SMP 反模式（全局调度抑制标志），未来误用即炸。
- **修复建议**: 改 `percpu()->no_reschedule_depth`，与 current_ 迁移一致。
- **关联 GOTCHA**: 无

---

## ✅ 审计中确认已正确处理的项（对照清单，非债务）

> 并发/SMP 维度审计确认 F4 核心同步原语质量高，以下**非债务**，记录在此防重复报告：
> - GOTCHA#21（current_ 静态）✅ 已修：`Scheduler::current()` 读 `percpu()->current`
> - GOTCHA#23（context_switch 恢复点读 per-CPU current）✅ 已修
> - GOTCHA#24（prepare-to-wait）✅ 已修：Mutex/Sem/futex/waitpid 四处正确，lost-wakeup 窗口关闭
> - GOTCHA#25/#26（GS_BASE 清零 / 长模式禁 mov %gs）✅ 已修：`GDT::load()` 只 flush ds/es/ss
> - lockdep per-CPU held stack + schedule-while-locked assert + AB-BA 图 ✅
> - RoundRobin runqueue `irq_guard()` 保护，pick_next 原子取出 ✅
> - per-CPU idle 任务 / SharedCwd/SharedSigActions 原子 refcount / reschedule IPI + cli recheck ✅
> - `next_tid`/`next_stack_vaddr` 已是 `lib::Atomic` ✅
> - IRQ 路径不 sleep（IRQ0→PIT→tick→schedule 合法抢占点）✅
