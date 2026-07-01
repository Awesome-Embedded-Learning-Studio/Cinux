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

## 审计维度计划（14 维度）

> 用户要求「记录打算从哪些维度排查」。权威方法见 `audit-guide.md`。每维度：读真实代码取证 → grep 坐实 → 写一次性 report → 登记高价值发现。

| # | 维度 | 状态 | 备注 |
|---|------|------|------|
| D1 | 架构不变量 | ✅ 已审 2026-06-21 | pass(无异常/RTTI/禁用头零命中,架构铁律严守)；见 `reports/2026-06-21-d1-d8-d10-d12-audit.md` |
| D2 | 内存生命周期（悬垂/UAF/buffer/所有权） | ✅ 已审 2026-06-20 | 见 `reports/2026-06-20-memory-smp-audit.md` |
| D3 | SMP / 并发安全（F4 多核后） | ✅ 已审 2026-06-20 | 见 `reports/2026-06-20-memory-smp-audit.md` |
| D4 | 进程 / 线程生命周期 | ✅ 已审 2026-06-21 | DEBT-002 坐实(exit 无 cleanup)；见 `reports/2026-06-21-d4-d13-audit.md` |
| D5 | 调度 / 迁移 / CPU 上下文 | ✅ 已审 2026-06-21 | F4 SMP 清洁(GOTCHA#23/25/26 全 pass)；见 `reports/2026-06-21-d5-d6-audit.md` |
| D6 | 用户 / 内核边界 | ✅ 已审 2026-06-21 | DEBT-019(用户指针非 copy)+ DEBT-012(phnum)；见 `reports/2026-06-21-d5-d6-audit.md` |
| D7 | 错误处理 / 崩溃韧性 | ✅ 已审 2026-06-21 | FO 清洁(panic 仅不变量/backtrace/memstats 全 pass)；见 `reports/2026-06-21-d7-d11-audit.md` |
| D8 | 测试覆盖盲区 | ✅ 已审 2026-06-21 | warn(875+49 test 广;user-mode/SMP 盲区=GOTCHA#11 已知)；见 `reports/2026-06-21-d1-d8-d10-d12-audit.md` |
| D9 | 静态 / 动态检查工具 | ✅ 已审 2026-06-21 | F-INFRA/F4-M5/Q1 清洁(UBSAN/lockdep/host-ASAN/static_assert 全 pass)；见 `reports/2026-06-21-d14-d9-audit.md` |
| D10 | 文档 / 可追溯性 | ✅ 已审 2026-06-21 | pass(TODO 仅 3 处 + PLAN/debt/notes 体系完整)；见 `reports/2026-06-21-d1-d8-d10-d12-audit.md` |
| D11 | 模块组织 / 可维护性 | ✅ 已审 2026-06-21 | 源全 <500(max 496)+ check_line_limits 排除 test/；见 `reports/2026-06-21-d7-d11-audit.md` |
| D12 | 发布 / 回归 / 变更管理 | ✅ 已审 2026-06-21 | pass(commit 规范严守,无 Co-Auth;高危用验证矩阵)；见 `reports/2026-06-21-d1-d8-d10-d12-audit.md` |
| D13 | 资源配额 / 非堆边界 | ✅ 已审 2026-06-21 | DEBT-018(kMaxCpus 不一致)；见 `reports/2026-06-21-d4-d13-audit.md` |
| D14 | 整数溢出 / 边界 | ✅ 已审 2026-06-21 | DEBT-020(ELF 字段算术)+ DEBT-012(phnum)；见 `reports/2026-06-21-d14-d9-audit.md` |

**进度**：**14/14 全审完成**（D1-D14，F-QA Q3 收官 2026-06-21）。deterministic 四段式方法论（A 锚点 / B 不变点 / C 门槛 / D 闭环）已就绪 + 全量实战（F-QA Q2），见 `document/todo/quality/audit-guide.md`。新债 DEBT-018/019/020 + DEBT-002 精确坐实 → 喂 Q4。

### 子系统专项审计（F-QA follow-up，补 Q3 后合入的未审面）

| 子系统 | 审计日期 | 维度 | 结论 | 报告 |
|---|---|---|---|---|
| xHCI/USB | 2026-06-25（F-CLN 批0）| D2+D3+D4 | D2/D4 清洁（零堆分配 + 全局静态 RAII）；**D3 fail → DEBT-021**（poll_events 无锁多上下文并发）| `reports/2026-06-25-xhci-usb-audit.md` |
**修债进展**：**Q4✅ 收官（2026-06-21）** —— Q4a 类型先行（RefCount/UserPtr）+ Q4b-e 修 6 债（DEBT-001/002/003/004/005/006 全 ✅,见各条）。9 批 + 1 fix（feat/f-qa-q4）。验证 run-kernel-test 887/0 + -smp 2 + LOCKDEP + host-ASAN 全绿。详见 `document/notes/2026-06-21-f-qa-q4-debt-convergence.md`。

**F-CLN✅ 收官（2026-06-25）** —— 批0 xHCI/USB 专项审（登记 DEBT-021 poll_events 并发,留 xHCI 重构）+ 批1-7 修 7 债（DEBT-015 栈帧/016 ASSERT_OK/018 kMaxCpus/008 signal VMA/009 huge/010 FDTable refcount/007 quantum per-task 全 ✅,见各条）。feat/f-cln-debt。验证 run-kernel-test 931/0 + -smp 2 + LOCKDEP + host ctest 54/0 全绿。残留 open:DEBT-019/013/020/012(留 F10 顺手)+011/014(低危)+021(xHCI 并发)。详见各批 notes `document/notes/2026-06-25-f-cln-b{0..8}-*.md`。

---

## 根因归纳（最重要的洞察）

两份报告（内存安全 + 并发 SMP）**独立交叉印证**，用户痛点「内存偶现挂死、四处崩溃」的燃料集中在**两个互为因果的系统性洼地**：

1. **进程/线程生命周期没闭环** —— 退出不释放（DEBT-002）+ CoW 无 mapcount（DEBT-003）+ CLONE_VM 无 refcount（DEBT-006）。本质是缺「物理页/地址空间/任务」三者的**引用计数与退出清理基础设施**（Linux 用 `_mapcount` / `mm_struct` refcount / `do_exit→exit_mm` 全套解决）。
2. **F4 漏网的全局可变状态** —— registry（DEBT-001）/ PidAllocator（DEBT-005）/ waiting_for_child（DEBT-004）。都在 F4 关注的调度器主线之外，单核严格串行永不显现，多核偶现炸。

> 当前 `-smp 2`「干净」很可能是 AP 仍主要跑 idle、并发压力没压到这些路径；多进程 + kill + fork 频繁交织时，DEBT-001/003/004 必现。这三条合起来几乎可确定是「偶现挂死/崩溃」的头号嫌疑。

**建议收敛方向**（待用户拍板，不在本表执行）：上述洼地天然适合收敛成一个「进程生命周期与引用计数」里程碑（exit cleanup + CoW mapcount + AddressSpace refcount + registry/PidAllocator 加锁 + waiting_for_child 原子化），拆批走 PR。

---

## 🔴 Critical

### DEBT-001 `g_registry_head` 全局任务注册表完全无锁 → 跨核并发崩溃 ✅
- **维度**: 并发/SMP　**优先级**: P0　**状态**: ✅ 已修（F-QA Q4c-2,`928b645`）　**核验**: ✅ grep 坐实
- **位置**: `kernel/proc/signal.cpp:36,47-77,153-175`
- **现象**: 裸全局单链表 `Task* g_registry_head`，`signal_register_task`(add_task 调) / `signal_unregister_task`(exit 调) / `signal_find_task_by_pid`(sys_kill 调) / `killpg`(sys_kill pid<0 调) 全部直接操作链表，**周围零锁匹配**。killpg 注释自称「no extra locking is needed」——该判断在多核下错误。
- **根因**: F4 未触碰 signal.cpp 注册表；单核严格串行永不触发，多核 fork/exec/kill 交织 → 头插写与遍历读并发 → 读半链接指针 / 悬垂 `registry_next` → 跳飞或 UAF（Task 被 slab 复用后 signal_send 读别对象数据）。
- **修复建议**: 给 `g_registry_head` 加全局 irq-safe `Spinlock`（add_task 可能 IF=0）。遍历持锁；`signal_send`→terminate 不能持锁，先收集 pid 列表再释锁发送。
- **关联 GOTCHA**: 无（OPEN GOTCHAS 未记录注册表并发）

---

## 🟠 High

### DEBT-002 退出任务的 TCB / 核栈 / 地址空间永不释放（系统性泄漏）✅
- **维度**: 内存安全(D4)　**优先级**: P1　**状态**: ✅ 已修（F-QA Q4e-2 正常路径 `3983fe6` + Q4e-3 异常 deferred-free `4bb6ca4` + reap unregister `e6ce2f4`）　**核验**: ✅ **Q3-1 坐实**：`remove_task`(scheduler.cpp:190)仅 test/ 调用，production(sys_exit/waitpid/exit_current)从不调 → release_resources 永不触发，Task+资源彻底泄漏。见 `reports/2026-06-21-d4-d13-audit.md`
- **位置**: `kernel/syscall/sys_exit.cpp:34-74` / `kernel/proc/scheduler.cpp:210-240`(exit_current) / `kernel/proc/process_new.cpp:120-221`(waitpid reap)
- **现象**: `sys_exit` 只置 Zombie + dequeue + yield；`exit_current` 标 Dead + context_switch 切走；waitpid reap 标 Dead + free pid + 解链 —— **三者都不 delete Task / free 核栈 / delete addr_space**。`release_resources` 只在 operator delete 内调，而 operator delete 只在 fork/clone error-path 触发，正常退出从不跑。
- **根因**: 缺 task exit cleanup。每个退出进程泄漏 Task(1008B slab)+4 页核栈+整棵 PML4 子树页表。长时间跑 shell 逐步耗尽 KMEM_SLAB 与物理页。更是 DEBT-003/006 的放大器（不做它，引用计数无从谈起）。
- **修复建议**: waitpid reap 中 `delete target`（→release_resources 释放 sig_actions/cwd/fd_table）+ 释放核栈（`g_vmm.unmap`+`g_pmm.free_pages`，注意 direct-map 不 unmap，GOTCHA#7）+ `delete addr_space`（析构 free_subtree）。CLONE_THREAD 共享 addr_space 需 refcount，最后线程才释放（联动 DEBT-006）。
- **关联 GOTCHA**: #11（exit_current leak，待 task exit cleanup）

### DEBT-003 CoW 物理页无引用计数 → fork+exec use-after-free ✅
- **维度**: 内存安全　**优先级**: P0　**状态**: ✅ 已修（F-QA Q4b-1 元数据 `0a4ba1c` + Q4b-2 fork/execve `34a4595` + Q4b-3 cow fault `037a08d`）　**核验**: ✅ grep 坐实（`grep mapcount` 零结果）
- **位置**: `kernel/proc/fork.cpp:49-93` / `kernel/proc/process_new.cpp:70-114`(handle_cow_fault) / `kernel/proc/execve.cpp:62-111`(clear_user_mappings)
- **现象**: fork `copy_page_table_level` 把可写 PTE 标 `FLAG_COW` 并共享**同一物理页**（`dst_table[i].raw = src_table[i].raw`，物理地址不变），**物理页无任何 refcount**。`clear_user_mappings`(execve) 叶子层**无条件** `free_page` 不检查 FLAG_COW/共享。
- **根因**: 经典 fork+exec：子 execve → free 掉与父 CoW 共享的物理页 → 父进程 PTE 仍指向已释放页 → PMM 重分配 → 父读垃圾/踩坏别人。**确凿的「正常用法触发 UAF」**。根因 = CoW 缺物理页引用计数（Linux `_mapcount`）。
- **修复建议**: 引入物理页 `_mapcount`（buddy order_ 数组旁加 int16）。fork CoW 共享页 `_mapcount++`；clear_user_mappings free 前每页 `_mapcount--`，归 0 才真 free；CoW fault 复制后旧页 `--`、新页 `=1`。F3 CoW 共享内存里程碑核心前置。
- **关联 GOTCHA**: 无（PLAN 列 CoW 未做但未标 UAF 风险）

### DEBT-004 `waiting_for_child` 普通 bool 跨核非原子 → lost-wakeup 偶现挂死 ✅
- **维度**: 并发/SMP　**优先级**: P0　**状态**: ✅ 已修（F-QA Q4c-1,`7b72659`）　**核验**: ✅ grep 坐实
- **位置**: `kernel/proc/process.hpp:259`(`bool waiting_for_child`) / `kernel/proc/process_new.cpp:215,218`(父 CPU 写) / `kernel/syscall/sys_exit.cpp:57`(子 CPU 读)
- **现象**: `waiting_for_child` 普通 bool。父 CPU waitpid 写 true/false，子 CPU exit 读 `task->parent->waiting_for_child` 决定是否 `unblock(parent)` —— 跨 CPU 无 atomic/无内存屏障。
- **根因**: 子 CPU 读到 stale 值（父刚置 true 但子还见 false）→ 不唤醒 → 父永睡。**头号偶现挂死嫌疑**。F4-M4 prepare-to-wait 修了 waitpid 自身 check-block 窗口，但漏了子 exit 侧对 parent 标志的非原子读。
- **修复建议**: 最干净 —— sys_exit **无条件** `unblock(parent)`（unblock 已幂等，仅 Blocked 态入队），去掉 `waiting_for_child` 门控，彻底消除 stale 读窗口。或改 `lib::Atomic<bool>` + Acquire/Release。
- **关联 GOTCHA**: 无（F4-M5 注释自称已分析 children 无需锁，但漏了此标志位的跨核可见性）

### DEBT-005 `PidAllocator` 无锁 → 双核分配相同 pid ✅
- **维度**: 并发/SMP　**优先级**: P1　**状态**: ✅ 已修（F-QA Q4d,`389987c`）　**核验**: ⚠️ 待核验（agent 报告，未亲验）
- **位置**: `kernel/proc/pid.cpp:14,20-59`；调用点 `sys_waitpid`(process_new.cpp:163) / fork/clone
- **现象**: 全局单例 `g_pid_alloc` 无内嵌锁，`alloc` 的 check-then-set（`if(!in_use_[candidate]) in_use_[candidate]=true`）非原子。
- **根因**: 两核同时 fork → 同一 candidate 都见 `!in_use_` → 都置 true → **两 task 拿相同 pid** → 注册表键冲突 / find_task_by_pid 返错 / waitpid 认错子。
- **修复建议**: `PidAllocator` 内置 irq-safe `Spinlock`，alloc/free/is_allocated 持锁；或 atomic bitmap + CAS。
- **关联 GOTCHA**: 无

### DEBT-015 syscall handler 栈帧过大（char[PATH_MAX] 缓冲置栈，4-8KB/16KB 栈）✅
- **维度**: 内存安全(栈)　**优先级**: P1　**状态**: ✅ 已修（F-CLN 批1,2026-06-25）　**核验**: ✅ big_kernel -Wframe-larger-than=1024 零命中
- **闭环**: 8 个 syscall(creat/mkdir/unlink/rmdir/open/chdir/stat/path)早年改 `PathBuf`(堆,path.cpp:19 "was char[PATH_MAX] on the stack");**批1 补最后残余 `sys_dmesg`** `LogEntry[16]`(272B×16=4.4KB)改 `new[]`/`delete[]` 堆。big_kernel 生产零 frame 命中。**门禁**:`-Wframe-larger-than=1024` warning 级保留(big_kernel_common PUBLIC);**GCC 技术限制**——`-Werror=frame-larger-than` GCC 拒绝(带参 warning 名不支持 -Werror= 升级),故硬门禁靠审计 grep 非构建失败;`big_kernel_test` `-Wno-frame-larger-than`(test fixture 栈大是设计)。详见 `document/notes/2026-06-25-f-cln-b1-debt015-frame-size.md`。
- **位置**: `kernel/syscall/sys_creat.cpp:29,44`(8272B) / `sys_mkdir.cpp:74`(8256) / `sys_unlink.cpp:74`(8240) / `sys_rmdir.cpp:103`(8288) / `sys_open.cpp:72`(4144) / `sys_chdir.cpp:78`(4144) / `sys_stat.cpp:75`(4224) / `sys_dmesg.cpp:109`(4400) / `kernel/fs/path.cpp:88`(4096)
- **现象**: 9 个 syscall/path handler 在 16KB 核栈上放 `char resolved[PATH_MAX]`(4096) + 常第二个 `char parent_buf[PATH_MAX]` → 单帧 4-8KB。`-Wframe-larger-than=1024` 全部命中。
- **根因**: path 解析缓冲放栈上(对齐 POSIX PATH_MAX)；sys_creat 尤甚(两个 PATH_MAX=8KB)。16KB 栈下 syscall 上下文 + 中断嵌套 + 调用链(lookup/create)有溢出风险。Linux 用 `getname`/`struct filename` 堆分配 path。
- **修复建议**: path 缓冲改堆(`kmalloc(PATH_MAX)` + RAII 释放,复用 F2-M7b slab)或专用 per-call path 缓冲设施;目标单帧 <1024B。修后启用 `-Wframe-larger-than=1024 -Werror=frame-larger-than` 入门禁。
- **验证建议**: 修后 `-Wframe-larger-than=1024` 零命中 → 升 `-Werror=frame-larger-than`;`timeout 40 run-kernel-test` 绿(碰 syscall 路径)。
- **关联 GOTCHA**: 无

### DEBT-021 `XHCIController::poll_events` 无锁，多上下文并发 → event ring 数据竞争
- **维度**: 并发/SMP(D3)　**优先级**: P1　**状态**: 🆕 登记待办（F-CLN 批0 审计发现）　**核验**: ✅ 读码坐实
- **位置**: `kernel/drivers/usb/xhci_controller.cpp:272-302`(poll_events) + `:387-391`(event_irq_thunk ISR) + `kernel/proc/init.cpp:53`(gui_worker 每帧) + `xhci_controller.cpp:311,331`(run_command/run_transfer busy-poll) + `kernel/drivers/usb/xhci_ring.cpp:61-76`(EventRing::dequeue)
- **现象**: `poll_events` 完全无锁，被三个上下文调：① gui_worker 线程每帧(init.cpp:53) ② usb::init 枚举线程的 run_command/run_transfer busy-poll(:311/:331) ③ MSI-X ISR event_irq_thunk(:389，QEMU+nested-KVM 下潜伏)。`EventRing::dequeue`(xhci_ring.cpp:61) 非原子——读 `dequeue_`/检查 cycle/`++dequeue_`/wrap flip `ccs_` 全无保护。
- **根因**: 设计假设"poll_events 单上下文串行"，但代码三处调。**当前①②两线程就并发**（kernel_init_thread 跑 usb::init 时 gui_worker 已 launch 持续 pump，见 init.cpp:36-58）。`dequeue_` 指针竞争 → 两线程读同 slot → 事件重复消费/丢失 + ERDP 写竞争错位 + listener on_transfer_complete 重入 inject。**侥幸不炸**：usb::init 枚举快(0.94s)窗口短 + gui_worker pump 时 event ring 多空。真机/新 QEMU MSI-X 中断触发后③加入，更剧烈(含同 CPU ISR 重入)。
- **修复建议**: poll_events 加 irq-safe Spinlock（acquire 覆盖 dequeue 循环 + ERDP 写 + listener 分发），对齐 Linux xHCI `spin_lock_irqsave` 单上下文化；EventRing::dequeue 持锁即安全。listener on_transfer_complete 若 inject SPSC 队列，锁内串行即恢复单生产者。
- **关联 GOTCHA**: 无（xHCI 并发未记）

---

### DEBT-022 ProcFS /proc/<pid>/{stat,cmdline} TOCTOU UAF（find_by_pid 锁外解引用 Task*）✅
- **维度**: 并发/SMP(D3)　**优先级**: P1　**状态**: ✅ 已修（fix/procfs-toctou-snapshot，2026-06-30）　**核验**: ✅ 读码坐实（signal_find_task_by_pid 锁内返指针 + format_proc_stat 锁外解引用 7 字段）
- **位置**: `kernel/proc/signal.cpp:77-87`(signal_find_task_by_pid 锁内返 Task*) / `kernel/fs/procfs.cpp:259-266,274-281`(stat/cmdline read 锁外 format_proc_stat) / `kernel/fs/procfs_content.cpp:79-93`(解引用 t->pid/name/state/ppid/tgid/uid/gid)
- **现象**: ProcFS read 经 `signal_find_task_by_pid` 在 `g_registry_lock` 下取 `Task*`，锁释放后 `format_proc_stat` 在锁外解引用。signal.cpp:79-81 + F6-M2 note 53 行注释「tasks 永不释放，安全（DEBT-002 未修）」**前提已失效**：DEBT-002 在 F-QA Q4e-3（4bb6ca4/e6ce2f4）已修，task 现经 exit_current→reap_deferred deferred-free。SMP(-smp 2)：CPU0 `cat /proc/<pid>/stat` 拿 t→unlock；CPU1 该 task exit→`delete t`；CPU0 format_proc_stat 解引用已 free Task → **UAF**。/proc 经 sys_open/sys_read 可达，两条 syscall 不关抢占/IRQ。
- **根因**: find_by_pid 返回原始指针供锁外用；DEBT-002 修复（task 真释放）使旧「永不释放」契约失效，注释未同步。
- **修复建议**: 对齐 killpg(signal.cpp:201-217)范式——加 `signal_snapshot_task(pid, TaskSnapshot&)`，ProcFS stat/cmdline read **锁内 snapshot 字段**(pid/name/state/ppid/tgid/uid/gid 拷到栈；name 拷字节不拷指针)到栈，锁外只格式化。不动 registry 接口。长期：Task refcount（find 时 acquire，reap release-then-free，RCU-safe registry）。
- **关联**: DEBT-002（registry 释放，同族）/ 同族 UAF [[reap-deferred-oncpu-uaf]] [[smp-migration-context-race]]
- **最低动作**: 立刻订正 signal.cpp:79-81 + note 的过时「安全」注释，防误导。
- **✅ 已修（fix/procfs-toctou-snapshot，2026-06-30）**: 加 `TaskSnapshot` POD（process.hpp，name 拷字节防悬）+ `signal_snapshot_task(pid,snap&)` 锁内拷字段（signal.hpp/cpp，对齐 killpg 范式但 snapshot 值非指针）+ ProcFS stat/cmdline read 改走 snapshot（format 签名 `const Task*`→`const TaskSnapshot&`）+ 订正 find_by_pid 过时注释。run-kernel-test-all 两 leg 1020/0 + host 69/69。详见 [note](../../notes/2026-06-30-debt-022-procfs-toctou-snapshot.md)。

## 🟡 Medium

### DEBT-018 `kMaxCpus` 两处定义不一致（同名不同值不同类型）✅
- **维度**: 资源配额(D13)　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批3,2026-06-25）　**核验**: ✅ grep 坐实改名
- **闭环**: 两处语义本不同——`cinux::proc::kMaxCpus=8`(运行 CPU 上限,percpu_blocks/idle_tasks/gdt/g_held 用)vs `cinux::drivers::acpi::kMaxCpus=16`(MADT 记录容量,cpu_apic_ids 用)。不同 ns 不撞 ODR 但同名易混。**acpi 那个改名 `kMaxAcpiLapics`**(区分语义:ACPI 可能报 >运行上限的 LAPIC,ap_main 用 proc::kMaxCpus 限 AP 启动)。ap_main.cpp 加 `static_assert(proc::kMaxCpus <= acpi::kMaxAcpiLapics)`(记录容量须 ≥ 运行上限,否则拓扑静默截断)。验证 run-kernel-test 931/0 + **-smp 2** ALL PASSED。详见 `document/notes/2026-06-25-f-cln-b3-debt018-kmaxcpus.md`。
- **位置**: `kernel/drivers/acpi/acpi.hpp:150`(`constexpr size_t kMaxCpus = 16`) / `kernel/proc/percpu.hpp:28`(`constexpr uint32_t kMaxCpus = 8`)
- **现象**: `kMaxCpus` 两处定义：acpi=16(size_t)、percpu=8(uint32_t)。数组用不同值——`percpu_blocks[8]`/`idle_tasks_[8]`/`gdt_blocks[8]` 用 8；`cpu_apic_ids[16]` 用 16。`ap_main.cpp:189,260` `cpu < proc::kMaxCpus`(8)保护避免 OOB。
- **根因**: (1) ACPI 报 >8 CPU 时 AP 静默不启动（丢弃，不报错）；(2) 同名常量两值违反单一定义（ODR 风险，编译期 TU 间可静默选其一）；(3) 类型不一致(size_t vs uint32_t)。当前 QEMU -smp 2 不触发，多核(>8)暴露。
- **修复建议**: 统一单一 kMaxCpus（建议 percpu=8 为权威，acpi 改用之；或显式 `ACPI_MAX_LAPIC=16` 区分 ACPI 表容量 vs 运行 CPU 上限）。加 static_assert 两处相等或删其一。
- **关联 GOTCHA**: 无

### DEBT-006 CLONE_VM 共享地址空间无引用计数 → 线程退出损坏共享页表 ✅
- **维度**: 内存安全　**优先级**: P2　**状态**: ✅ 已修（F-QA Q4e-1 RefCount `7ddda74` + Q4e-2 release `3983fe6`）　**核验**: ⚠️ 待核验
- **位置**: `kernel/proc/clone.cpp:266-271`（注释自承 `// shared (no refcount yet)`）/ `kernel/proc/process.hpp:176`
- **现象**: clone CLONE_VM 时 `child->addr_space = parent->addr_space` 无 refcount。配合 DEBT-002（退出不 delete）目前侥幸不出事。
- **根因**: 一旦加 exit cleanup，共享 addr_space 的线程退出若 `delete addr_space` → 兄弟线程 PML4 整棵 free → 全员崩；反之不释放则最后线程退出时永久泄漏。多线程程序「偶现崩溃」高度可疑于此。
- **修复建议**: `AddressSpace` 加原子 refcount；clone CLONE_VM 时 acquire；线程退出 release，归 0 才 delete。与 DEBT-002 同批。
- **关联 GOTCHA**: 无

### DEBT-007 `quantum_remaining_` 单一共享 quantum → 多核时间片错乱 ✅
- **维度**: 并发/SMP　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批7,2026-06-25）　**核验**: ✅ -smp2 + 单核回归
- **闭环**: quantum 从 RoundRobin 类单一成员(`quantum_remaining_`)→ **per-task 字段**(`Task::quantum_remaining`,对齐 Linux `task_struct->rt.time_slice`)。原 multi-core bug:两核 tick 各自递减同一 `quantum_remaining_` → 实际时间片变 `DEFAULT_TIME_SLICE/ncpus`,一核 recharge 重置另一核正在跑的任务。改动:process.hpp Task 加 `int32_t quantum_remaining`;scheduler.hpp RoundRobin 删成员;roundrobin.cpp ctor/pick_next/clear/task_tick 改用 `task->quantum_remaining`;task_fork + TaskBuilder::build 设 child/新任务满量子(DEFAULT_TIME_SLICE)。验证 run-kernel-test 931/0 + **-smp 2** ALL PASSED + host ctest 54/0(单核 per-task 等价旧共享)。详见 `document/notes/2026-06-25-f-cln-b7-debt007-quantum-per-task.md`。
- **位置**: `kernel/proc/roundrobin.cpp:40,109,142-156`
- **现象**: `default_rr_` 全局单例的 `quantum_remaining_` 被 `lock_.irq_guard()` 保护（不崩溃），但两核 tick 各自递减同一变量。
- **根因**: 实际时间片变 `DEFAULT_TIME_SLICE / ncpus`，一核耗尽 recharge 影响另一核正在跑的任务。**行为错非崩溃**，调度不可预测。
- **修复建议**: quantum 改 per-task（`Task::quantum_remaining`）或 per-CPU，对齐 Linux `task_struct->rt.time_slice`。
- **关联 GOTCHA**: 无

### DEBT-008 signal_setup_frame 写信号帧不校验栈 VMA → 二次 segfault ✅
- **维度**: 内存安全　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批4,2026-06-25）　**核验**: ✅ 读码 + 冒烟
- **闭环**: signal_setup_frame(signal.cpp:301)计算 R 后、写帧前加 VMA 校验——`current()->addr_space->vmas().find(R)` 须命中可写 Stack VMA(has_flag Write+Stack),否则 `signal_exec_default(task, sig)` fallback(默认终止)而非写帧。避免深栈收信号 R 越 guard → 写帧中途 PF → 栈损坏 + 原信号 in-flight → 偶现挂死。IF=0 ISR 上下文,vma_lock.irq_guard() no-op 锁(文档临界区)。验证 run-kernel-test 931/0(kernel-mode 不覆盖 user-mode signal_setup_frame)+ make run 冒烟启动到桌面无 panic/无 [SIGNAL] fallback。边界(栈溢出收信号)靠逻辑正确性,留真用户态信号程序端到端 follow-up。详见 `document/notes/2026-06-25-f-cln-b4-debt008-signal-vma.md`。
- **位置**: `kernel/proc/signal.cpp:265-315`
- **现象**: `R = user_rsp - pad - 8 - sizeof(SignalFrame)`(~160B) 后写信号帧/trampoline，**不查 R 是否落合法 Stack VMA / 是否越 guard page**。
- **根因**: 深递归栈近底时收信号 → R 落 guard page 或 VMA 外 → 触发 PF → 投 SIGSEGV，但此刻信号帧写一半、栈已损坏、原信号正投递中 → 语义混乱的「偶现挂死」。Linux 投递前 expand_stack / 查 altstack。
- **修复建议**: 投递前校验 `[R, user_rsp)` 落 Stack VMA 内；否则改用 sig_altstack 或直接 SIGSEGV 默认终止。
- **关联 GOTCHA**: #11（PF 硬门控+栈增长，未覆盖信号帧写入）/ #16（sigreturn trampoline，未覆盖）

### DEBT-009 clear_user_mappings 不识别 huge page entry → 误当 PT 页释放 ✅
- **维度**: 内存安全　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批5,2026-06-25）　**核验**: ✅ 读码
- **闭环**: 三处加 huge entry 检测(防御,当前 NXE off 无 user huge 不触发):clear_user_mappings(execve.cpp)PDPT 层 1GB + PD 层 2MB、free_subtree(address_space.cpp)递归各层。huge entry 是数据页非页表——原代码下钻解析 huge 内容当 PT + free garbage 物理页 → PMM 错乱。修:遇 huge → kprintf warn + 清零 entry + continue(不下钻不 free)。huge free(buddy order 2MB/1GB)未实现,留真正 huge 支持里程碑(检测到 huge 说明该里程碑漏更新此路径)。验证 run-kernel-test 931/0 + 编译零 warning。关联 GOTCHA#13(direct-map huge split,相关但针对 VMM.map)。详见 `document/notes/2026-06-25-f-cln-b5-debt009-huge-detect.md`。
- **位置**: `kernel/proc/execve.cpp:81-105`
- **现象**: 4 层遍历叶子层 `free_page`，中间层也 `free_page`，**全程不检查 `entry.huge`（PS bit）**。
- **根因**: 用户空间若引入 2MB/1GB huge（mmap/brk 未来可能），huge entry 基址被当 PDPT 表页 free，并向下把 huge 内容当 PT 解析 → free garbage 物理页 → PMM 状态错乱。当前潜伏。
- **修复建议**: 每层先判 `entry.huge`：huge entry 直接 `free_page(phys_addr)` 并清零，不向下走。`AddressSpace::~AddressSpace` 的 free_subtree 同样需补。
- **关联 GOTCHA**: #13（huge split 破坏 direct-map，相关但针对 VMM.map）

### DEBT-010 `FDTable` refcount 用 `guard()` 非 `irq_guard`，与 R3 不一致 ✅
- **维度**: 并发/SMP　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批6,2026-06-25）　**核验**: ✅ -smp2 回归
- **闭环**: acquire/release 改 `__atomic_*_fetch(&refcount_, 1, ACQ_REL)`(对齐 SharedCwd/SharedSigActions R3),去 `lock_.guard()` + racy `refcount_>0` 守卫(正确生命周期不 underflow)。release 到 0 独占(无并发)读 fds_[]+close(持锁)+delete。alloc/close/get/set 保留 `lock_.guard()`(fds_[] 数组保护,非 refcount;当前 IRQ 不触达 FDTable,未来触达再升 irq_guard)。验证 run-kernel-test 931/0 + host ctest(fd_table/pipe/shell_redirect/shell_write/sys_pipe 全过)+ **-smp 2** ALL PASSED。详见 `document/notes/2026-06-25-f-cln-b6-debt010-fdtable-refcount.md`。
- **位置**: `kernel/fs/file.cpp:29-54`
- **现象**: `FDTable::acquire/release` 用 `lock_.guard()`（非 IRQ-safe），对照 SharedCwd/SharedSigActions（F4-M5 R3）已改 `__atomic_*_fetch(ACQ_REL)`。
- **根因**: 当前 IRQ 路径不碰 FDTable，不立刻死锁。但属「未爆但脆」的同步原语选型不一致 —— 未来任何 IRQ handler 触达 FDTable 即本核持锁重入死锁。
- **修复建议**: 统一 `irq_guard()`，或像 SharedCwd 把 refcount 改 atomic（单字段独立于 fds_[]，release 到 0 再持锁清理）。
- **关联 GOTCHA**: 无（R3 范围明确只覆盖 SharedCwd+SharedSigActions）

### DEBT-016 test fixture 忽略 ErrorOr 返回值（32 处，[[nodiscard]] 触发）✅
- **维度**: 测试覆盖(D8)　**优先级**: P2　**状态**: ✅ 已修（F-CLN 批2,2026-06-25）　**核验**: ✅ -Wunused-result 零命中
- **闭环**: 两套 framework 各加 `ASSERT_OK` 宏（非 void-safe,失败 abort/exit 不 return,区别于 TEST_ASSERT 的 `return;`）：`big_kernel_test.h` 用 QEMU isa-debug-exit(io_outb 0xf4←1),`test_framework.h` 用 `_TEST_ABORT()`(host abort/QEMU hlt)。32 处忽略全包:big_kernel_test 29 处(test_ramdisk 17 mount + test_ext2 系 4 result.ext2->mount + vfs/cwd_stat/file_mmap/page_cache get_page/shell_write/syscall_ext2/ahci_write)+ host 3 处(test_shell_redirect write)。去 `test/CMakeLists.txt:16` 全局 + `kernel/CMakeLists.txt:299` big_kernel_test 的 `-Wno-unused-result`。验证 run-kernel-test 931/0 + host ctest 54/0 + 零 ignoring。详见 `document/notes/2026-06-25-f-cln-b2-debt016-assert-ok.md`。
- **位置**: `kernel/test/test_ramdisk.cpp`(17 处 mount 忽略) / `test_ext2*`+`test_cwd_stat`/`test_ahci_write`/`test_file_mmap`/`test_shell_write`/`test_syscall_ext2`(~11 处 ext2 mount 忽略) / `kernel/test/test_page_cache.cpp:237`(get_page) / `test/unit/test_shell_redirect.cpp:205-207`(write)。**生产 kernel 代码零忽略**。
- **现象**: ErrorOr class 加 `[[nodiscard]]`(F-QA Q1-2)后,32 处 test fixture 忽略 ErrorOr 返回值触发 `-Wunused-result`。多为 setup helper 内 `ext2/ramdisk->mount()` 忽略 + 少量 get_page/write。
- **根因**: test 沿用"setup 忽略错误"习惯。尝试用 `TEST_ASSERT_TRUE` 清,但该宏失败时 `return;`(无值),不能用在返回 `Ramdisk*`/`AhciExt2Pair` 的非 void setup helper → 编译 error。需非 void-safe 的检查原语。
- **修复建议**: 给 test framework(big_kernel_test.h + test_framework.h)加 `ASSERT_OK(expr)` 宏(失败时 abort/exit,不 `return;`,非 void-safe),32 处改用 `ASSERT_OK`,去掉 `big_kernel_test` + `test/` 的 `-Wno-unused-result`(F-QA Q1-2 临时压制)。
- **验证建议**: 清完后去 `-Wno-unused-result`,编译零警告,`run-kernel-test` + `make test_host` 绿。
- **关联 GOTCHA**: 无

### DEBT-017 ✅ host ASAN findings（OOB + 泄漏 + double-free）— 已修（F-QA Q2，2026-06-20，feat/f-qa-q2）
- **维度**: 内存安全(D2) + 测试(D8)　**优先级**: P1　**状态**: ✅ 已修（F-QA Q2）　**核验**: ✅ ASAN 坐实 + 修复后全绿
- **误诊订正**: 原登记「`RingBuffer::push_batch` 边界 bug」**错误**。push_batch 对 `buffer_` 自身安全（`tail_%N` 保 [0,N)，`!full()` 保不溢）；越界是**调用方传错 count**。**真因不在 push_batch，不在 Cinux-Base 子模块**——4 处、3 类独立问题，全在主仓库 test/ + 一处 kernel 防御。
- **真根因（4 处，3 类）**:
  1. **OOB**（`test/unit/test_pipe.cpp:458`）`try_write("BBBB", 200)` 把 5 字节字面量当 200B buffer → push_batch 读越界（global-buffer-overflow @ ring_buffer.hpp:73 的 `items[]` 侧，非 `buffer_`）。修：真实 200B buffer。
  2. **泄漏 776**（`test/unit/test_fd_table.cpp`，18624B/776 alloc）栈构造 `FDTable table;` 不调 `release()`（栈上调会 `delete this` 崩），旧设计无析构 → alloc 的 File 不释放。修：**`kernel/fs/file.cpp` 加 `~FDTable()` 兜底释放**（对 release 路径幂等：close 已设 nullptr，析构 delete nullptr no-op；同时强化 production 资源安全不变量）。
  3. **泄漏 1**（`test/unit/test_multi_terminal.cpp:745`，24104B）`add_window(new Terminal)` 满返 0（wm 不接管所有权）+ test 未 delete。修：overflow 后 `delete`。
  4. **double-free 9 处**（`test/unit/test_sys_pipe.cpp`）FDTable 析构暴露的 test ownership bug：`set()` 接管 File 所有权（docstring「ownership transferred to FDTable」），但 test 误以为「caller owns, delete manually」手动 delete FDTable 持有的 File → 加析构后 double-free。修：删 9 处手动 delete（归 FDTable 析构），保留被 replace 出表的旧 File（caller 负责，docstring「previous File released」）。
- **production 影响**: 零。`RingBuffer::push_batch` production 调用（pipe/keyboard）chunk 守护 `min(remain,space)` 安全；FDTable production 经 `release()/close()` 释放（析构幂等 no-op）；sys_pipe production File 归 `current_fd_table()` 不手动 delete。
- **验证**: host ASAN 全绿（Debug + Release(-O2)+ASAN+UBSAN+FORTIFY CI 对等，全量 `make test_host` 100%）+ `run-kernel-test` 875/0（FDTable 析构无回归）+ 编译零警告。ci.yml host-tests flip `-DCINUX_HOST_ASAN=ON` 硬门禁。
- **残留异味**（登记，非本债）: `test/unit/test_shell_redirect.cpp` `~PipeRedirect` 的 `delete stdin_file` 侥幸安全——构造函数局部变量 shadow 同名私有成员（成员恒 nullptr），析构 delete nullptr no-op；消除 shadow 即 double-free。备查。
- **关联 GOTCHA**: 无

### DEBT-023 Pipe/FIFO 的 fd-close 不传到 Pipe（无端引用计数 → POLLHUP/EOF 漏）
- **维度**: 资源生命周期(D4) + 正确性(C)　**优先级**: P2　**状态**: 🆕 登记待办（F8-M5 审计，2026-07-01）　**核验**: ✅ grep 坐实
- **位置**: `kernel/fs/file.cpp`(FDTable::close 已调 `inode->ops->release`) / `kernel/ipc/pipe_ops.cpp`(PipeReadOps/PipeWriteOps 未 override release) / `kernel/ipc/pipe.hpp`(Pipe 无 read/write 端引用计数)
- **现象**: F8-M5 给 `InodeOps` 加了 `release`(FDTable::close 关 fd 时调),socket 正确释放(`SocketOps::release → Socket::close`)。但 **Pipe/FIFO 未 override release**(默认 no-op)——关 pipe 的写端 fd 不会调 `Pipe::close_writer()`,故 reader 测不到 POLLHUP/EOF;shell pipeline(`ls|grep`)靠"最后 writer 关 fd 才 EOF"的语义不成立。pre-existing(F8-M1 起即如此),本批只是登记。
- **根因**: 正确的 last-close 需要 **Pipe 端引用计数**——一个 Pipe 的读/写端可能被多个 File 引用(dup/fork 各拷一份 File→同 inode),fork 后跨 FDTable 共享。Per-FDTable 扫描不够(漏跨表引用)。需:File 构造 bump inode refcount,析构 dec,到 0 才 release;再由 Pipe 跟踪"几个读端/写端 inode 活着"(FIFO 多 open 共享一 Pipe)。两层计数。仓促做会压垮 pipeline EOF(第一个 dup-close 就发 EOF)。
- **修复建议**: ① Inode 加 `refcount`,`File` 构造/析构 bump/dec,到 0 调 release;② Pipe 加 `read_refs_/write_refs_`,PipeReadOps/PipeWriteOps `open`/release 各 ±1,到 0 才 `close_reader/close_writer`;③ 验证:`ls|grep` grep 收 EOF 退出、POLLHUP-on-fd-close 测、fork 后 pipeline 不早 EOF。
- **关联 GOTCHA**: 详见 `document/notes/2026-07-01-f8-m5-real-poll.md` follow-up。

---

## 🟢 Low

### DEBT-019 用户指针 validate 后直接解引用（非 copy_to_from_user，PF 兜底）
- **维度**: 用户/内核边界(D6)　**优先级**: P3　**状态**: 🆕 登记待办（F-QA Q3-2 审计）　**核验**: ✅ grep 坐实（零 copy_from_user/copy_to_user）
- **位置**: `kernel/syscall/path_util.hpp:26`(`validate_user_ptr` 只查 canonical address) / 各 syscall(sys_stat/sys_pipe/sys_creat 等)validate 后直接解引用用户指针
- **现象**: CinuxOS 用户边界用 `validate_user_ptr`(canonical address 检查)+ 直接解引用,PF handler(F2-M5 硬门控:user PF 无 VMA→segfault)兜底。**非 Linux copy_to_from_user + access_ok 模型**。
- **根因**: (1) validate 不查映射存在/权限/长度(多字节结构跨页未映射→PF,kernel-mode 解引用容错零页);(2) 多核 TOCTOU(user 另核改映射 + kernel 解引用 race);(3) 不对齐 Linux copy 模型。当前单核 + 用户态串行不触发;多核用户态 + SMP 理论风险。
- **修复建议**: 未来多核用户态时引入 copy_to_from_user(access_ok + 长度 + 页 copy),或至少 validate 查 VMA + 长度。当前 PF 兜底可接受(单核)。
- **F9 批4 更新（SMAP 启用）**: 合法访用户经 syscall/ISR entry STAC 显式放行,意外（漏 STAC）访用户 → SMAP #PF（双层保护,不再仅 PF 兜底）。validate 行为不变;完整 copy_from_user 修复仍留 F10。
- **关联 GOTCHA**: #11(PF 硬门控 user-mode 判定)

### DEBT-020 execve ELF 字段算术无溢出检查（恶意/损坏 ELF → VMA 映射错乱）
- **维度**: 整数溢出/边界(D14)　**优先级**: P3　**状态**: 🆕 登记待办（F-QA Q3-4 审计）　**核验**: ✅ 读码坐实
- **位置**: `kernel/proc/execve.cpp:218`(seg_end = p_vaddr + p_memsz + PAGE_SIZE-1) / `:256`(p_offset + seg_offset) / `:189`(phnum * sizeof,DEBT-012)
- **现象**: ELF phdr 字段(p_vaddr/p_memsz/p_offset/p_filesz)参与算术无溢出检查。`validate_elf_header`(L181)只校验 ehdr,不校验 phdr 算术。p_vaddr + p_memsz 若 wrap(UINT64_MAX 附近)→ seg_end 错乱 → VMA/页表映射错乱(L267 map)。
- **根因**: ELF 字段用户可控(损坏/恶意 ELF)。当前 init/shell 是仓库编译 ELF(字段合法,不触发);未来 execve 用户自定义 ELF + 恶意构造触发。read 兜底部分(ReadFailed)但 seg_end 用于 VMA 映射(L267 map)。
- **修复建议**: validate 扩展 phdr:p_vaddr + p_memsz 不溢出 + 在用户地址空间范围(USER_BRK_MAX 等)+ p_offset + p_filesz ≤ inode->size。拒绝越界/wrap phdr。
- **F-VERIFY M0-2 核实（2026-06-27）**: `-fsanitize=unsigned-integer-overflow` 是 **Clang-only**，GCC 工具链（本项目 CI + toolchain-x86_64）编译器报 `unrecognized argument`；**不能用 UBSAN flag 兜底 unsigned wrap**。必须 validate 内显式 checked 运算（saturating-add / checked-mul 拒绝 wrap）。audit 假设的 flag 路线作废。
- **关联**: DEBT-012(phnum 无上限,同 validate 漏)

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
