# Cinux — 代码质量债务登记表（Code Quality Debt Registry）

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
| LLM 全仓静态扫描 | 2026-08-14 | 七维度（A workaround / B 隐性 TODO / C 过度抽象 / D 并发 / E 资源 / F 错误处理 / G 风格），15 find + skeptic 逐条复核 + 2 补扫 | 62 confirmed + 18 补扫 → **DEBT-025~046**（22 条族归并）；**killpg 订正为活跃 UAF**（DEBT-002 前提已失效）；audit 未覆盖 scheduler preempt 弧 signal_check（战役批次 C 人工补查）| `reports/2026-08-14-llm-static-audit.md` |
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

### DEBT-025 killpg 锁后用裸 `Task*` 发信号——**活跃 UAF**（DEBT-002 前提已失效）
- **维度**: 生命周期/并发(D2/D3)　**优先级**: P0　**状态**: 🆕 登记待办（2026-08-14 LLM 审计,critical）　**核验**: ✅ skeptic 复核 + 登记时订正坐实
- **位置**: `kernel/proc/signal.cpp:276-291`(killpg)
- **现象**: killpg 在 `g_registry_lock` 下快照 ≤64 个 Task* 进栈数组，**释锁后**循环 `signal_send(targets[i])`。signal.cpp:276 注释自称安全前提 "tasks are never freed (DEBT-002 not yet fixed)"——**该前提已失效**：DEBT-002 于 2026-06-21 修复（`4bb6ca4`/`e6ce2f4`），task 现经 `exit_current→reap_deferred→delete` 真释放（同文件 :73 注释自证）。target 可在 unlock 与 send 之间 exit+delete → 跨 CPU UAF / 信号发给 slab 复用后的无关 task。
- **根因**: 与已修 DEBT-022（ProcFS TOCTOU）完全同族的姐妹点——Q4e-3 修复让「永不释放」契约失效，DEBT-022 修了 /proc 却漏了 killpg。`itimer_real_tick` 的 `expired[]` 快照同模式（signal.cpp:310 区域）。
- **修复建议**: signal_send 循环期间持 registry lock（queue_signal 不睡眠，持锁安全——DEBT-001 修时已论证过「terminate 需释锁」仅对 signal_send→exit_current 路径,故改为锁内 queue_signal+收集,锁外 terminate）;或 per-task refcount（acquire on lookup / release after send,对齐 Linux tasklist_lock+get_task_struct,亦可一并治 DEBT-024）。
- **复现策略**: **stress**——-smp 2 下 fork 大量子进程进同 pgid + 并发 kill(-pgid) + 子进程自发 exit,配合 slab poison/RACE_DETECT;窗口窄,先修后压测防回归为主,同时手写确定性交错单测（mock 释锁点）验证不变量。
- **关联**: DEBT-022 / DEBT-024（parent 裸指针,同生命周期根）;报告 B-critical 节。

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

### DEBT-004 waitpid 条件检查与 child-exit 发布非原子 → lost-wakeup 偶现挂死 ✅
- **维度**: 并发/SMP　**优先级**: P0　**状态**: ✅ 已修（F-QA Q4c-1 `7b72659` 初修；2026-08-12 production 双 GCC 复验补齐原子握手）　**核验**: ✅ 确定性交错测试 + 单核/SMP 953/953
- **位置**: `kernel/proc/process_new.cpp`(`prepare_child_wait` / `publish_child_exit`) / `kernel/syscall/sys_exit.cpp` / `kernel/proc/process.hpp`(`waiting_for_child`)
- **现象**: 父任务先扫描 children 未见 Zombie，尚未发布 Blocked 时子任务退出；子侧 `unblock(parent)` 看到父仍 Running 而无操作，父随后睡眠且再无通知。短命的 GCC/assembler/`ls` 子进程在双核下可把 shell 永久挂在 waitpid。
- **根因**: 原修只处理了 bool 的跨核可见性，没有把“检查退出条件”和“发布等待状态”与子侧 Zombie 发布放进同一个同步协议；单纯 atomic bool 仍关不住 scan→sleep 窗口。反过来，无条件 generic `unblock(parent)` 也不安全：父可能正等 futex/vfork/pipe/IO，会被错误唤醒且可能破坏 intrusive wait queue。
- **闭环**: 单个 IRQ-safe child-exit lock 串行化 children/Zombie 扫描、精确 `waiting_for_child` 标记与 Blocked 发布；子侧在同锁内发布 Zombie，并且只为真正的 waitpid waiter 生成 wake，锁外再 signal/unblock。`exit_and_reap_current` 用 `IrqSaveGuard` 保证本 CPU 不会在 Zombie publish 与 parent/vfork wake 之间丢掉退出 continuation。
- **门禁**: parent-first、child-first（旧 scan→prepare 精确窗口）和“Blocked 但在等其他对象，不得被 child exit 唤醒”三类机制测；production 独立单核/SMP 均 953/953。
- **关联 GOTCHA**: `Task::parent` 裸指针生命周期仍见 DEBT-024；原子握手不声称修复 orphan reparent。

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

### DEBT-024 `Task::parent` 裸指针跨父任务 deferred-free → 子退出 UAF
- **维度**: 生命周期(D2/D4)　**优先级**: P0　**状态**: 🆕 登记待办（2026-08-12 NVMe/preempt 收官审计）　**核验**: ✅ production 调用链坐实
- **位置**: `kernel/proc/process.hpp`(`Task::parent`) / `kernel/proc/scheduler.cpp`(`exit_current`→`reap_deferred` delete) / `kernel/syscall/sys_exit.cpp:58-78`(子退出 `signal_send(parent)` + `unblock(parent)`)
- **现象**: kernel thread/launcher 可经 `exit_current()` 直接进入 deferred-free，但它 fork 的子任务仍保存裸 `parent`。父 TCB 被下一轮 schedule 删除后，子任务退出会解引用该地址发送 SIGCHLD并尝试 unblock；slab 若已复用该槽，可向无关 Task 投信号/改 runqueue，未复用时也是 UAF。
- **根因**: DEBT-002 让 Task 真释放后，原有 parent 指针的“父活得比子久”隐含契约失效；当前既没有 orphan reparent，也没有 parent Task refcount/PID lookup lifetime。
- **修复建议**: 对齐 Unix 生命周期，在父退出锁定 children 链并把存活子任务 reparent 给 init/PID1；子退出只通过受生命周期保护的 parent 引用通知。备选是 Task refcount + registry lookup acquire/release，但不能只在 sys_exit 做空指针判断（悬垂值非 null）。补“父先退出→子后退出→init reap”真实 SMP 测试。
- **范围说明**: 该债早于本次 NVMe hang；原冻结发生在任何 child exit 之前，不是 owner/head/CID mismatch 根因。单独排期，避免在抢占根修里 ad-hoc 改进程语义。

### DEBT-026 sa_mask 与本信号 handler 期间不 block → 同号重入损坏 sigreturn
- **维度**: 正确性/信号　**优先级**: P0　**状态**: 🆕 登记待办（2026-08-14 LLM 审计,high）　**核验**: ✅ skeptic 跨文件复核（sys_signal.cpp:57 正确存 / signal_frame.cpp:21 `[[maybe_unused]]` 丢弃,literal TODO 自承）
- **位置**: `kernel/proc/signal_frame.cpp:21-22`（入口 `kernel/syscall/sys_signal.cpp:46,57,134`）
- **现象**: `signal_setup_frame()` 接到 `act.sa_mask` 直接丢弃。handler 执行中同号（或 sa_mask 中）信号再到达 → 重入 setup_frame 在用户栈叠第二帧 → sigreturn 恢复垃圾寄存器/RIP。busybox/musl 普遍设 sa_mask,跨所有用自定义 handler 的里程碑。附带：SA_RESETHAND/SA_RESTART 同样存了从不应用（signal_frame.cpp:133）。
- **修复建议**: SignalFrame 加 `sig_blocked_saved`,建帧时 `task->sig_blocked |= (sa_mask | mask(sig))`（SA_NODEFER 不加自身）,sigreturn 恢复;SA_RESETHAND 投递后翻回 kDefault。须配合 DEBT-040 的 sig 锁/原子。
- **复现策略**: **deterministic**——用户态测试程序:装 handler(sa_mask 含 SIGUSR2),handler 内 raise 同号,断言不递归 + sigreturn 后寄存器完好(musl -O2 试金石对齐)。**首号复现靶**,红测试 → 修 → 绿。
- **关联**: 报告 A-high / B-high 节;DEBT-040。

### DEBT-027 AHCI 共享 DMA 缓冲 `dma_buf_` 全程无锁（NVMe/virtio 同课漏修）
- **维度**: 并发/SMP(D3)　**优先级**: P0　**状态**: 🆕 登记待办（2026-08-14 LLM 审计,high）　**核验**: ✅ skeptic spot-check 坐实（ahci_block_device.hpp 无 `lock_` 成员 vs nvme_block_device.hpp:81 `Mutex lock_;`）
- **位置**: `kernel/drivers/ahci/ahci_block_device.hpp:74`（read_blocks/write_blocks/flush）+ `kernel/drivers/ahci/ahci.cpp:217`（execute_command 硬编码 slot 0 无端口锁）
- **现象**: read/write 都 memcpy 进 dma_buf_、把 phys 交 AHCI、memcpy 出,零互斥。NVMe hpp 明文记录为何要锁（"production -smp 2 + gcc fork chain once exposed DmaBuffer corruption"）,virtio_blk 同样有 Spinlock,**唯独 AHCI 漏了**。生产 -smp 两 CPU 并发块读重叠缓冲 → 静默 ext2 块损坏。init.cpp boot-disk 兜底仍走 AHCI,是活的。附带 execute_command 复用 slot 0,并发交错 cmd_tbl → Frankenstein FIS。
- **修复建议**: 照 nvme_block_device.cpp:47 抄——`cinux::proc::Mutex lock_;` 包住 read/write/flush 体;AHCI 级再加 per-port Spinlock 跨 execute_command。
- **复现策略**: **stress**——-smp 2 + AHCI 盘,两线程并发 read_blocks 校验已知 pattern（写后并发读比对）;test_ahci_write 已有 AHCI 测试环境可扩。数据校验型,不需 RACE_DETECT。
- **关联**: DEBT-021（xHCI,同「多上下文无锁」模式）;报告 D-high 节。

### DEBT-028 VMA 存储 `vma_lock_` 声明了却只 signal_frame 一处用,fault 与 mmap/munmap/mprotect 跨 CPU 无锁
- **维度**: 并发/SMP(D3)　**优先级**: P0　**状态**: 🆕 登记待办（2026-08-14 LLM 审计,high）　**核验**: ✅ skeptic 复核（grep 全部 acquirer,唯一 signal_frame.cpp:44）
- **位置**: `kernel/mm/address_space.hpp:147`（vma_lock_）+ `kernel/arch/x86_64/page_fault.cpp:187`（fault 读）+ `kernel/syscall/sys_mmap.cpp:156,167,184,189` / `sys_munmap.cpp:233` / `sys_mprotect.cpp:265,292,293,301`（并发改/遍历）
- **现象**: page-fault 在 CPU0 IF=0 下读 `vmas().find(fault_addr)`,sys_mmap/munmap/mprotect 在 CPU1 splice/free 同一 intrusive 链表节点且无锁。IF=0 只屏蔽本 CPU 中断,不挡另一 CPU syscall。典型 torn-list/UAF 竞态——CinuxOS 历史最大 bug 源一类。基础设施已存在,只是没用。
- **修复建议**: fault 路径（IF=0,irq_guard 幂等）与 syscall 路径全部 `vmas()` 访问取 `vma_lock_`;fork/execve 的地址空间拷贝同查。
- **复现策略**: **deterministic-ish**——先给 vmas() 访问点铺 `lockdep_assert_held(vma_lock_)`,-smp 2 跑现有测试 + mmap/munmap churn 压测,LOCKDEP 直接报未持锁（F-DYN-COV 基建现成）;加锁后同压测防回归。
- **关联**: DEBT-008（signal VMA 校验,当年 no-op 锁文档临界区）;报告 D-high 节。

### DEBT-029 net 栈全无锁（TcpModule/UdpModule/IcmpModule/NetStack/LoopbackDevice/ephemeral port）
- **维度**: 并发/SMP(D3)　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计,high×2）　**核验**: ✅ skeptic 复核（net/ grep Spinlock/guard 零命中）
- **位置**: `kernel/drivers/net/net_init.cpp:71`（net_poll kthread vs socket syscall）+ `kernel/net/loopback_device.cpp:21`（storage_/state_/fifo_/head_/tail_）+ `kernel/net/tcp.hpp:244`（cons_/listens_）+ `kernel/net/icmp.cpp:76`（raw_sockets_）+ `kernel/net/tcp_socket.cpp:82`（function-local static eph++ 非原子）
- **现象**: net_poll kthread 在一 CPU 扫 g_tcp/udp/icmp/arp/stack 表,app 线程在另一 CPU 改同一批表——close() 置 `ports_[i].l=nullptr` 窗口期 poll on_udp() deref → UAF/重复分发;loopback send_l3 vs poll_rx+recycle 破 ring 不变量;TCP ephemeral 端口两核 connect 撞同端口。AF_INET 127.0.0.1 主路径,-smp 开网络立刻 race。
- **修复建议**: 各模块一把 Spinlock（表查找/修改持锁,FSM/TX 不进锁段——send_segment 回发不能持锁睡眠/DMA）;短期最低限度 net_poll_entry 与 socket syscall 用同一把 net 大锁串行化。ephemeral 端口对齐 UdpSocket 的持锁查重分配。
- **复现策略**: **stress**——-smp 2 + loopback:并发 UDP/TCP socket open/close/send/recv churn + net_poll 同跑,断言无 UAF/无串台;LOCKDEP 铺桩（同 DEBT-028 法）先行。
- **关联**: 报告 D-high×2 + D-low×3 节（tcp/icmp/ephemeral 并入本条）。

### DEBT-030 输入路径 SMP 并发（ConsoleTty IRQ1 无锁 / pty_alloc TOCTOU / mouse 双生产者）
- **维度**: 并发/SMP(D3)　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 补扫,**未经 skeptic 复核**）　**核验**: ⚠️ 补扫 agent 读码坐实,待人工/复核确认
- **位置**: `kernel/drivers/tty/console_tty.cpp:90-131`（IRQ1 上下文改 tty_/reader_/foreground_pgid_,read/poll 只用本地 InterruptGuard——UP 掩盖）+ `kernel/drivers/tty/pty_device.cpp:442-451`（in_use check-and-claim 无锁,两 CPU 并发 open ptmx 抢同槽→共享同一 PTY 对）+ `kernel/drivers/mouse/mouse.cpp:184-207`（PS/2 IRQ12 与 xHCI inject 双生产者改同一组 statics,usb_primary_ 无 fence,破 SPSC）
- **现象**: 三处同模式——单核设计假设 + InterruptGuard 只关本 CPU IRQ,-smp 下另一核并发即真数据竞争。pty_alloc 同槽双主是确定性的语义破坏。
- **修复建议**: input() 用 Spinlock(irq_guard) 与 read/poll_events 共享（对齐 PtySlot slave_read 模式）;pty_alloc check-and-claim 前取锁;mouse 生产者串一把 Spinlock（InputEventDevice 已为同问题这么做）。
- **复现策略**: pty_alloc **host-testable**（test_multi_terminal 环境并发 open 断言无同槽）;ConsoleTty/mouse 需 mock IRQ 线程或 -smp 真键鼠（难,靠修复+审查+LOCKDEP）。
- **关联**: DEBT-021（同「多上下文无锁」模式）;报告补扫节。

---

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

### DEBT-023 Pipe/FIFO 的 fd-close 不传到 Pipe（无端引用计数 → POLLHUP/EOF 漏）✅
- **维度**: 资源生命周期(D4) + 正确性(C)　**优先级**: P2　**状态**: ✅ 已修（GCC 自举线，2026-07-01）　**核验**: ✅ 两 leg 1098/0 + host 69/69 + 机制测 dup-last-close
- **位置**: `kernel/fs/file.cpp`(FDTable::close 已调 `inode->ops->release`) / `kernel/ipc/pipe_ops.cpp`(PipeReadOps/PipeWriteOps 未 override release) / `kernel/ipc/pipe.hpp`(Pipe 无 read/write 端引用计数)
- **现象**: F8-M5 给 `InodeOps` 加了 `release`(FDTable::close 关 fd 时调),socket 正确释放(`SocketOps::release → Socket::close`)。但 **Pipe/FIFO 未 override release**(默认 no-op)——关 pipe 的写端 fd 不会调 `Pipe::close_writer()`,故 reader 测不到 POLLHUP/EOF;shell pipeline(`ls|grep`)靠"最后 writer 关 fd 才 EOF"的语义不成立。pre-existing(F8-M1 起即如此),本批只是登记。
- **根因**: 正确的 last-close 需要 **Pipe 端引用计数**——一个 Pipe 的读/写端可能被多个 File 引用(dup/fork 各拷一份 File→同 inode),fork 后跨 FDTable 共享。Per-FDTable 扫描不够(漏跨表引用)。需:File 构造 bump inode refcount,析构 dec,到 0 才 release;再由 Pipe 跟踪"几个读端/写端 inode 活着"(FIFO 多 open 共享一 Pipe)。两层计数。仓促做会压垮 pipeline EOF(第一个 dup-close 就发 EOF)。
- **修复建议**: ① Inode 加 `refcount`,`File` 构造/析构 bump/dec,到 0 调 release;② Pipe 加 `read_refs_/write_refs_`,PipeReadOps/PipeWriteOps `open`/release 各 ±1,到 0 才 `close_reader/close_writer`;③ 验证:`ls|grep` grep 收 EOF 退出、POLLHUP-on-fd-close 测、fork 后 pipeline 不早 EOF。
- **✅ 已修（GCC 自举线，2026-07-01）**: 两层计数落地。外层 `Inode::refcount`（`inode.hpp`）+ `inode_ref/inode_unref` helper（`file.cpp`：File 进出 FDTable 时 bump/dec，到 0 调 release；**File 退回纯 struct 不碰 inode**——避 test use-after-free，由 fd 点 alloc/set/dup/dup2 + close/displace 管 refcount，锁外 release）。内层 `Pipe::read_refs_/write_refs_`（`pipe.hpp`）+ add/release（`pipe.cpp`：irq_guard 内联 close 逻辑避重入）+ `PipeReadOps/PipeWriteOps` 构造 add / release override（`pipe_ops.cpp`：每端一 inode，FIFO cloning 自动覆盖）。机制测 `test_sys_pipe_dup_last_close_eof`（dup 后首个 close 不 EOF，last 才 BrokenPipe）。`ls|grep` pipeline EOF 语义正确。
- **关联 GOTCHA**: 详见 `document/notes/2026-07-01-f8-m5-real-poll.md` follow-up。

### DEBT-031 符号链接 follow 一族（access/chmod/chown/utimensat 仍查/改链接而非目标）
- **维度**: 正确性/VFS　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（对照已修 sys_stat/sys_open 用 vfs_lookup(Follow),四条遗漏同族）
- **位置**: `kernel/syscall/sys_access.cpp:73` / `sys_chmod.cpp:34` / `sys_chown.cpp:33` / `sys_utimensat.cpp:56`（附带:flags `[[maybe_unused]]` 丢 AT_SYMLINK_NOFOLLOW;times==NULL 用 0 占位非当前时间,make 增量构建判断错乱）
- **现象**: 四个 syscall 用 `fs->lookup(rel)` 不 follow 尾随 symlink。对 `/bin/sh`（→busybox 链接）access(X_OK) 查的是链接 mode;`touch file` 把 mtime 设 epoch 0。历史 sys-stat-follow-ls-denied 同族,当时只修了 stat。
- **修复建议**: 统一改 `vfs_lookup(path, Follow)`;utimensat 按 flags 切 NoFollow,times==NULL 用 boot_epoch_ns()+mono（sys_clock_gettime.cpp:55-57 基础设施现成）。
- **复现策略**: **deterministic**——建 symlink 指向已知 mode 文件,断言 access/chmod/chown 作用于目标;touch 后 stat 断言 mtime≈now。红→修→绿。
- **关联**: GOTCHA sys-stat-follow;报告 A 节。

### DEBT-032 unchecked-new OOM null-deref 一族（vma/clone/tmpfs/dentry/dedicated_caches/AddressSpace）
- **维度**: 错误处理(F)　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（-fno-exceptions 下 operator new→kmalloc 耗尽返 nullptr,crt_stub.cpp:191 坐实）
- **位置**: `kernel/mm/vma.cpp:46`（make_node,注释「Heap exhaustion traps via operator new」对本 build 事实错误）+ `kernel/proc/clone.cpp:338,342` 与 `fork.cpp:479-487`（new FDTable/File 无 guard）+ `kernel/fs/tmpfs/tmpfs_ops.cpp:71,114`（new uint8_t[newcap] 后直接 memcpy）+ `kernel/fs/dentry.cpp:86` + `kernel/mm/dedicated_caches.cpp:30`（create_cache null 静默留全局）+ `kernel/mm/address_space.cpp:64`（OOM 打 FATAL 返半初始化对象）
- **现象**: 无异常内核 new 失败返 nullptr,六处直接 deref → OOM 时 #GP/#PF 而非 -ENOMEM propagate。违背「错误走 ErrorOr」铁律。vma.hpp 注释宣称的失败模型是错的,误导后来者。
- **修复建议**: 逐处 null 检查 + propagate Error::OutOfMemory（make_node→insert 返 null 时 caller 返 OOM;tmpfs 对齐 ext2 的 kmalloc+检查范式;dedicated_caches 失败 boot kpanic 使 early OOM 立即可见）;订正 vma.hpp 误导注释。
- **复现策略**: **stress/构造**——测试内压低 slab 余量（大量分配后）再走 mmap/fork/tmpfs write,断言返 -ENOMEM 不崩;或给 kmalloc 加 test-only fail 注入点。host 单测可 mock。
- **关联**: 报告 F 节。

### DEBT-033 interruptible sleep 未实现——SIGKILL 打不断 futex/waitpid/Mutex/Semaphore 阻塞
- **维度**: 正确性/调度·信号　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计;**与 scheduler preempt 弧同根**）　**核验**: ✅ skeptic 复核（signal.cpp:217 / process.hpp:291 / scheduler_block.cpp:119 三处注释自承）
- **现象**: 只有 `wait_queue_head != nullptr` 的用户 IO 等待可被信号唤醒 EINTR;futex(FUTEX_WAIT)/waitpid/Mutex/Semaphore 阻塞是纯不可中断的。SIGKILL/SIGSTOP 对 futex-waiter 无响应,只能等自然醒。跨里程碑 TODO 无时间线。
- **修复建议**: 给 Mutex/Semaphore/futex/waitpid 的 prepare_to_wait 路径接 signal_deliverable_pending 检查（像 schedule_blocked IO 分支）,让致命信号能打断;与 preempt 弧的 voluntary preempt point（Mutex/Semaphore 退出点）天然同批。
- **复现策略**: **deterministic**——线程 A FUTEX_WAIT,线程 B kill -9 A,断言 A 在期限内退出（当前红:不退出）。**先补查 preempt 弧 signal_check 内核态路径**（审计未覆盖,B1 #GP 现场）再动。
- **关联**: scheduler preempt 弧（B0 done B1 卡）;报告 B-medium 节。

### DEBT-034 getdents64 小缓冲静默丢失已推进 offset 的目录条目
- **维度**: 正确性/VFS　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（offset 先推进后判满,line 66-68 注释自承 entry lost）
- **位置**: `kernel/syscall/sys_getdents64.cpp:64-68`
- **现象**: 循环先读条目（内部 offset++）再判 `off+reclen > count` 超限 break——刚读的条目因 offset 已越过永久丢失。Linux 保证不丢（放不下则不消费）。小缓冲遍历目录 → ls/find/cp -r 偶发漏文件不可恢复。
- **修复建议**: 「读取」与「offset 推进」解耦——peek 不推进,放得下才消费（对齐 Linux readdir 模型）。
- **复现策略**: **deterministic**——建 N 条目目录,用 1×reclen 大小缓冲循环 getdents64 拼回全量,断言条目数不丢。红→修→绿。
- **关联**: 报告 B-medium 节。

### DEBT-035 page_cache invalidate_range 丢 read 错误留全零页冒充文件内容
- **维度**: 错误处理/内存(F+A)　**优先级**: P1　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（对照 get_page:118 正确 propagate,此处 `static_cast<void>` 丢弃）
- **位置**: `kernel/mm/page_cache.cpp:185`（write 后重读同步缓存路径）
- **现象**: `memset(v,0,ps)` 后把 `inode->ops->read()` 的 ErrorOr 整个丢弃。重读失败（NVMe/AHCI I/O 错、短读）→ 页面停留全零态,sys_read/mmap 把零页当文件真实内容。b3-ext2-agg-sigill 同族（I/O 失败静默掩盖→上层拿垃圾）。
- **修复建议**: 对齐 get_page:118——检查 read 返回,失败标 CachedPage invalid（下次 get_page 重填）或移除并返错,绝不留静默零页。
- **复现策略**: **host-testable**——test_page_cache 环境注入 fail-read 的 mock inode ops,断言 invalidate_range 后 get_page 返错误而非零页。
- **关联**: [[b3-ext2-agg-sigill-handoff]] 同族;报告 A-medium 节。

### DEBT-036 ext2 rename 正确性缺口（拒绝覆盖已存在目标 + 目录 rename 不调 nlink）
- **维度**: 正确性/ext2　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（line 220-224/258-260 注释自承 follow-up）
- **位置**: `libs/ext2/ext2_links.cpp:198-260`
- **现象**: `mv a b`（b 已存在）失败,POSIX 要求原子替换,构建脚本/make 依赖;移动目录后父目录 nlink 不 ±1 → stat 报错、find 异常、rmdir 永不成功。
- **修复建议**: 目标存在先 unlink 再 add;目录 rename 时 src 父 i_links_count--、dst 父 ++ 并 write_disk_inode。
- **复现策略**: **deterministic**——mv 覆盖断言成功+内容正确;mv 目录后 stat 父 nlink 断言。ext2 库有 host ASAN/TSAN 测试环境（f6-m6 双端直编）,host 侧先红后绿。
- **关联**: 报告 B-medium 节。

### DEBT-037 AF_UNIX stream 阻塞 send() 无 flow control,peer RX 满返 EAGAIN
- **维度**: 正确性/net-IPC　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（unix_socket.cpp:227-231 注释自承 for-now）
- **位置**: `kernel/net/unix_socket.cpp:227`（recv/accept 已正确用 wait queue,唯 send 缺）
- **现象**: 阻塞 socket 的 send() 在 peer 4KB ring 满时返 EAGAIN——正确阻塞的调用者死循环烧核,naive 调用者拿到 blocking socket 上的虚假 EAGAIN。
- **修复建议**: 镜像 recv():peer 满时 prepare_to_wait 到 send_waiters_,recv pop_batch 腾空间后 wake_one;honor EINTR（连 DEBT-033）。
- **复现策略**: **deterministic**——填满 peer ring 后阻塞 send,断言阻塞被唤醒（当前红:立即返 EAGAIN）。
- **关联**: DEBT-033;报告 B-medium 节。

### DEBT-038 驱动 init 吞错一族（AHCI void init / framebuffer 丢 map_2mb / e1000 EEPROM 哨兵+半复位继续）
- **维度**: 错误处理(F)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计+补扫）　**核验**: ✅ 主三条 skeptic 复核;e1000 两条补扫未复核
- **位置**: `kernel/drivers/ahci/ahci.cpp:290`（init 返 void,4 条失败路径静默 bail——production AHCI 空单例 #GP 历史）+ `kernel/drivers/video/framebuffer.cpp:35`（丢 map_2mb bool,失败后写未映射地址;对照 LAPIC/IOAPIC/HPET/MSI-X 都检查）+ `kernel/drivers/net/e1000.cpp:63`（read_eeprom 超时返 0xFFFF 被 read_mac 当有效广播 MAC+UPE,EEPROM 故障彻底掩盖）/ `:113`（reset 不自清仅警告继续）
- **修复建议**: AHCI init 改 `ErrorOr<void>`（对齐 NVMe/VirtIO）;framebuffer 检查+回滚;e1000 read_eeprom 返 ErrorOr、reset 失败 fail-fast,net_init 干净禁 net。
- **复现策略**: **QEMU 基本不可复现**（QEMU 硬件总正常）——代码修复+对照审查;AHCI init 可用「不存在的 BAR5」构造 fail 路径单测。
- **关联**: [[production-kernel-init-test-blindspot]]（AHCI 空单例 #GP 同族）;报告 F-medium + 补扫节。

### DEBT-039 IoPhys 缺页 map 失败静默 return——OOM 下无噪 #PF 死循环
- **维度**: 错误处理(F)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（注释称会 panic 但分支内无 panic 调用）
- **位置**: `kernel/arch/x86_64/page_fault.cpp:274`
- **现象**: 设备 mmap（/dev/fb0）缺页 map_nolock 失败（OOM）→ handler return → 同指令再 #PF → 无噪死循环,看似挂死连 backtrace 都抓不到。同文件 anonymous 路径会 fall through SIGSEGV/panic,唯 IoPhys「假装解决」。
- **修复建议**: 失败时用户态投 SIGBUS（对齐 :378 file-backed 读失败）,内核态 panic 带 "OOM-at-device-mmap" 诊断。
- **复现策略**: stress/构造——OOM 压力下 mmap fb0 触发;或 test-only 让 map_nolock 可注入失败。复现价值中等（修法明确）。
- **关联**: 报告 A-medium 节。

### DEBT-040 信号路径共享状态非原子族（sig_pending RMW / setpgid / itimer / IOAPIC）
- **维度**: 并发/SMP(D3)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（process.hpp:315 「aligned=atomic,TSO 下 benign」注释判错——read-decrement-write 非单条原子）
- **位置**: `kernel/proc/signal.cpp:204-220`（queue_signal 对 sig_pending/state/sig_forced 无锁 RMW,与目标 CPU 投递并发→丢信号/幽灵信号——跨 CPU 信号投递核心路径）+ `kernel/proc/process_group.cpp:28`（setpgid 无锁写 pgid vs killpg 持锁读）+ `signal.cpp:310`（itimer_real_tick(PIT IRQ) vs sys_setitimer 非原子 RMW→新计时值丢失）+ `kernel/drivers/apic/io_apic.cpp:34`（IOREGSEL/IOWIN 两步 RMW 无锁——当前仅 BSP bring-up 调,为未来埋）
- **修复建议**: Task 加 sig_lock(Spinlock) 或 sig_pending/sig_forced 改 atomic 位操作（对齐 Linux sighand->siglock）;itimer 用 per-task 锁或 CAS 减法;IOAPIC 加 Spinlock。
- **复现策略**: **stress**——-smp 2 反复 kill 跨核进程断言信号必达（丢位即红）;setitimer+ping 压测断言 SIGALRM 间隔。RACE_DETECT 辅助。sig_pending 丢信号是最值得复现的（POSIX 不可接受行为）。
- **关联**: DEBT-026（同批修,sa_mask 需要 sig 锁）;报告 D-medium/low 节。

### DEBT-041 PCIDevice::bar[] uint32 截断 64-bit BAR
- **维度**: 资源/契约(E)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核（pci.hpp:48 uint32_t[6],调用方按 64 位语义用,msix 注释自承限制）
- **位置**: `kernel/drivers/pci/pci.cpp:103` + `pci.hpp:48`
- **现象**: 64-bit BAR 高 32 位隐式截断;e1000/nvme/xhci/ahci/msix 全按 64 位读。QEMU BAR<4GB 不炸;真机大显存 NVMe/GPU 的 MMIO>4GB → 静默错地址。
- **修复建议**: `bar[]` 改 `uint64_t[BAR_COUNT]`。
- **复现策略**: **QEMU 不可复现**（BAR 总在 4GB 下）——类型修复+static_assert,审查。
- **关联**: 报告 E-medium 节。

### DEBT-042 过度抽象/死代码清理族（11 处,零消费者证据链齐全）
- **维度**: 可维护性(C)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计+补扫切面）　**核验**: ✅ 每处 grep override/instantiation/caller 数坐实（报告含证据链）
- **位置与清单**: ① `kernel/mm/vma.hpp:133` IVMAStore 单实现纯虚接口（AddressSpace 存具体类型,折叠）② `kernel/proc/scheduler.hpp:68` task_deadline 零调用零 override（含「测试断言死代码存在」循环论证）③ `scheduler.hpp:64` task_fork 生产 fork/clone 从不调（RoundRobin override 白写——或改真调,quantum 重置语义被静默跳过）④ `kernel/fs/vfs_lookup.hpp:36` LookupFlag::Create/Excl 全仓无调用 ⑤ `kernel/net/net_device.hpp:51,56,60` max_frame/supports_zerocopy/link_up 零决策调用（doc 还与实现不符）⑥ `kernel/lib/user_ptr.hpp:45` UserPtr 零生产消费者（DEBT-019 脚手架,头注释自承）⑦ `kernel/mm/phys_ref.hpp:93-98` AnonPhysRef/PageTablePhysRef 零实例化 ⑧ `kernel/gui/data/icon_data.hpp` 整文件 ~280 行死代码（零 include）⑨ `kernel/gui/event.hpp:110` EventQueue+Mouse::poll 死代码但 gui_init.cpp:38 keyboard listener 仍往死队列 enqueue（每键一次白拷贝）⑩ `kernel/drivers/canvas.hpp:177` draw_bitmap colorkey hack 无调用者 ⑪ scheduler.hpp:114 MAX_CLASSES=4 生产只注册 1 类（**建议保留**——可测试性收益真实,降 2+注释即可）
- **修复建议**: ①-⑩ 删除（机械可逆,真需要时再加）;③ 二选一:fork/clone 真调或删钩子;⑪ 保留+MAX_CLASSES=2+退化注释。
- **复现策略**: 无需复现——grep 证据链已闭环;删除后全量编译+两 leg 验证绿即闭环。
- **关联**: [[xhci-hid-layering-antipattern]]（CODING-TASTE §13）;报告 C 节+补扫切面。

---

## 🟢 Low

### DEBT-019 用户指针 validate 后直接解引用（非 copy_to_from_user，PF 兜底）
- **维度**: 用户/内核边界(D6)　**优先级**: P3　**状态**: 🆕 登记待办（F-QA Q3-2 审计）　**核验**: ✅ grep 坐实（零 copy_from_user/copy_to_user）
- **位置**: `kernel/syscall/path_util.hpp:26`(`validate_user_ptr` 只查 canonical address) / 各 syscall(sys_stat/sys_pipe/sys_creat 等)validate 后直接解引用用户指针
- **现象**: Cinux 用户边界用 `validate_user_ptr`(canonical address 检查)+ 直接解引用,PF handler(F2-M5 硬门控:user PF 无 VMA→segfault)兜底。**非 Linux copy_to_from_user + access_ok 模型**。
- **根因**: (1) validate 不查映射存在/权限/长度(多字节结构跨页未映射→PF,kernel-mode 解引用容错零页);(2) 多核 TOCTOU(user 另核改映射 + kernel 解引用 race);(3) 不对齐 Linux copy 模型。当前单核 + 用户态串行不触发;多核用户态 + SMP 理论风险。
- **修复建议**: 未来多核用户态时引入 copy_to_from_user(access_ok + 长度 + 页 copy),或至少 validate 查 VMA + 长度。当前 PF 兜底可接受(单核)。
- **F9 批4 更新（SMAP 启用）**: 合法访用户经 syscall/ISR entry STAC 显式放行,意外（漏 STAC）访用户 → SMAP #PF（双层保护,不再仅 PF 兜底）。validate 行为不变;完整 copy_from_user 修复仍留 F10。
- **关联 GOTCHA**: #11(PF 硬门控 user-mode 判定)

### DEBT-020 execve ELF 字段算术无溢出检查（恶意/损坏 ELF → VMA 映射错乱）✅ 已修
- **维度**: 整数溢出/边界(D14)　**优先级**: P3　**状态**: ✅ 已修（feat/boost_cinux B1, 2026-07-08）　**核验**: ✅ 已修坐实
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

### DEBT-012 execve phnum 无上限校验 → 损坏 ELF 触发 3.6MB 分配 ✅ 已修
- **维度**: 内存安全　**优先级**: P3　**状态**: ✅ 已修（feat/boost_cinux B1, 2026-07-08,上限 256）　**核验**: ✅ 已修坐实
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

### DEBT-043 syscall flag/资源语义族（socketpair 泄漏 / O_CLOEXEC 未接 / SOCK_NONBLOCK 静默丢 / pipe inode 不释放 / master_write count / killpg 64 截断 / waitpid 超时谎言）
- **维度**: 正确性/资源(E+B)　**优先级**: P2　**状态**: 🆕 登记待办（2026-08-14 LLM 审计+补扫）　**核验**: ✅ 主批 skeptic 复核;pty master_write 补扫未复核
- **位置与清单**: ① `kernel/syscall/sys_socketpair.cpp:64,70` 第二 fd 安装失败显式泄漏 fd0（注释自承 for-now;EFAULT 路径泄漏两个）② `kernel/syscall/sys_open.cpp:244-247` O_CLOEXEC 接受但未接 FDTable（F_SETFD 能设,alloc 路径漏——execve 泄漏进子进程）③ `kernel/syscall/sys_socket.cpp:196` accept4 的 SOCK_NONBLOCK 被吞,静默给阻塞 fd（比返错更危险）④ `kernel/syscall/sys_pipe.cpp:32` pipe close 释放 File 不释放 inode/ops/Pipe（shell 管道密集→单调内存增长;InodeOps 缺 release hook 系统级债,socket 同注）⑤ `kernel/drivers/tty/pty.cpp:54-66` master_write 无条件返回 count 但 input_char 可能丢字节（write 契约破坏）⑥ `kernel/proc/signal.cpp:277` killpg/itimer 固定 64 目标静默截断,超出进程收不到信号（make -j64+ 漏 SIGTERM→父 hang）⑦ `kernel/proc/process_new.cpp:339` waitpid 50M 自旋超时后「leak task+占 PID+返回 Ok」——危险谎言,PID 耗尽+Task 泄漏
- **修复建议**: ①②③ 补错路径 close/设 flag/返 EINVAL;④ InodeOps 加 release hook 一次解决 pipe+socket;⑤ 返回实际消费数;⑥ 动态分配或两遍扫描,超限 WARN;⑦ 超时 kpanic 或挂 orphan reap 链表后台重试。
- **复现策略**: ①③⑤⑥ **deterministic**（EMFILE 构造/断言新 fd 非阻塞/小 ring 填满/65 进程 killpg）;② execve 后断言 fd 已关;⑦ 较难（需构造 on_cpu 卡死）,修复+审查。
- **关联**: 报告 A/B 节+补扫;④ 连 DEBT-023 模式。

### DEBT-044 stale 误导注释族（ext2 agg / pit.hpp mode3 / mouse center / mini idt DPL / capture_first_gp / draw_bitmap）
- **维度**: 可维护性(G+B)　**优先级**: P3　**状态**: 🆕 登记待办（2026-08-14 LLM 审计+补扫）　**核验**: ✅ 逐处对照实现坐实
- **位置与清单**: ① `libs/ext2/ext2_common.cpp:130-163` 注释自称「agg OFF(follow-up)」实际 agg>1 激活 + buffer 名写 block_buf_（实为 scratch）——调试时按注释查状态完全扑空,掩盖已修复的正确性 ② `kernel/drivers/pit/pit.hpp:66-102` 仍称 square-wave generator(mode 3)+描述不存在的 uptime 打印——有人据注释改回 mode 3 会**重新引入已修的历史 2x 时序 bug** ③ `kernel/drivers/mouse/mouse.cpp:152` 注释 "start at center" 代码置 0（左上角）④ `kernel/mini/arch/x86_64/idt.cpp:84-85` 「DPL=3 allows INT3」但 0x8F 实为 DPL=0 ⑤ `kernel/arch/x86_64/fault_diag.cpp:149` static bool 只抓开机第一个 #GP,真正致命 fault 的 debug 信息永远丢 ⑥ `kernel/drivers/canvas.hpp:187` colorkey hack 注释群（连 DEBT-042⑩ 删）
- **修复建议**: ①②③④ 重写注释与实现对齐;⑤ capture 改 from_user==false/panic 时才触发;⑥ 随删除处理。
- **复现策略**: 无需复现——文档债,修后 grep 断言旧文案零命中。
- **关联**: [[pit-mode3-2x-timing-bug]]（②的历史根因）;报告 G/B 节。

### DEBT-045 输入/串口边角族（mouse overflow 位 / tty 256 静默丢 / Mouse::poll 吞事件 / serial putc 无界 / e1000 tx slot DD / pipe underflow 静默）
- **维度**: 正确性/契约(E)　**优先级**: P3　**状态**: 🆕 登记待办（2026-08-14 补扫,**未经 skeptic 复核**）　**核验**: ⚠️ 补扫 agent 读码坐实
- **位置与清单**: ① `kernel/drivers/mouse/mouse.cpp:213` X/Y_OVERFLOW 位定义了从不检查（快速移动 delta>255 静默截错值）② `kernel/drivers/tty/tty.cpp:139-146` canonical 行缓冲 256 满后字节静默丢（不回显不报错,commit_line/cooked_push 同）③ `kernel/drivers/mouse/mouse.cpp:391` poll 遇非 mouse 事件 return false 吞掉该事件 ④ `kernel/drivers/serial/serial.cpp:58` putc 无界忙等无超时——**kprintf/panic 路径**,串口卡死时 panic 不可见 ⑤ `kernel/drivers/net/e1000.cpp:270` send_packet 不等前一描述符 DD 即复用槽,环形回绕踩 in-flight DMA ⑥ `kernel/ipc/pipe.cpp:319` release_*_ref 下溢保护静默 return,掩盖不平衡 ref bug（连 DEBT-023）
- **修复建议**: ① 溢出位饱和 ±255 或删常量;② 溢出响铃 '\a'+文档化;③ continue 弹下一个;④ 有界重试+stuck flag 保 panic 输出;⑤ 复用槽前查 DD;⑥ 下溢 WARN（开发构建 panic）。
- **复现策略**: ②⑤⑥ **可测**（host 单测/tty 测试环境）;①③ 需真硬件;④ QEMU 不可复现（审查）。
- **关联**: 报告补扫节。

### DEBT-046 mini 早期内核族（handle_pf 死循环 / ata dma 超时当成功 / ring0 注释）
- **维度**: 错误处理(F)　**优先级**: P3　**状态**: 🆕 登记待办（2026-08-14 LLM 审计）　**核验**: ✅ skeptic 复核
- **位置与清单**: ① `kernel/mini/arch/x86_64/exception_handlers.cpp:98` #PF 打印后 IRETQ 返回→真缺页必然 RIP 不变死循环刷爆串口,把「未实现」当「安全继续」 ② `kernel/mini/driver/ata.cpp:261` 5000 万次轮询超时无标志继续走成功路径,把没传完的缓冲当完整数据交 ELF loader ③ mini idt DPL 注释（并入 DEBT-044④）
- **修复建议**: ① 无法修复的缺页 `cli;hlt` 停机+CR2/RIP/error code 诊断;② 超时 kprintf+return false+二次确认 ACTIVE 清零。
- **复现策略**: ① 可构造（mini 加载漏映射即触发,但复现价值低——修法明确）;② 需磁盘故障注入（跳过）。**mini 是引导期内核,优先级最低,可与 DEBT-044 合批顺手清**。
- **关联**: 报告 A-medium 节;FC29000 教训同源。

---

## ✅ 审计中确认已正确处理的项（对照清单，非债务）

> 并发/SMP 维度审计确认 F4 核心同步原语质量高，以下**非债务**，记录在此防重复报告：
> - GOTCHA#21（current_ 静态）✅ 已修：`Scheduler::current()` 用单条 `%gs:24` 读取本 CPU current，避免迁移时旧 PerCpu 指针
> - GOTCHA#23（context_switch 恢复点读 per-CPU current）✅ 已修
> - GOTCHA#24（prepare-to-wait）✅ 已修：lost-wakeup 窗口关闭；Mutex/Semaphore 另以 handoff/grant predicate 拒绝通用虚假唤醒
> - GOTCHA#25/#26（GS_BASE 清零 / 长模式禁 mov %gs）✅ 已修：`GDT::load()` 只 flush ds/es/ss
> - lockdep per-CPU held stack + schedule-while-locked assert + AB-BA 图 ✅
> - RoundRobin runqueue `irq_guard()` 保护，pick_next 原子取出 ✅
> - per-CPU idle 任务 / SharedCwd/SharedSigActions 原子 refcount / reschedule IPI + cli recheck ✅
> - `next_tid`/`next_stack_vaddr` 已是 `lib::Atomic` ✅
> - IRQ 路径不 sleep（IRQ0→PIT→tick→schedule 合法抢占点）✅
