# CinuxOS Infrastructure Audit Guide — 基建深度审计指南

> Tier 1.5（审计方法）。本文回答“从哪些维度严肃检查 CinuxOS 基建”。审计发现登记到 `debt.md`；每轮报告写入 `reports/`；每轮提交门禁见 `document/ai/QUALITY-GATES.md`。

## 0. 审计原则

- **先证据，后判断**：每条发现必须有文件、行号、grep 结果或调用链。
- **先高危，后整洁**：P0/P1 崩溃、挂死、UAF、数据损坏优先于命名、拆文件。
- **不把审计变成重构**：审计只登记；修复另开批次。
- **每条债务保留轨迹**：闭环后在 `debt.md` 标 ✅，不删除。
- **每轮审计一篇报告**：报告放 `reports/<date>-<dimensions>-audit.md`，记录范围、方法、发现和非债确认；`debt.md` 只保留可排期 backlog。
- **按风险域读代码**：不要只搜 TODO。很多致命问题没有 TODO。
- **deterministic 锚点优先（F-QA Q2）**：每个维度先跑固定 rg 锚点表、命中数进表，**跑完才读码**；不变点逐条 pass/fail/n/a 带 file:line，非「≥5 非债」凑数。例外：D5/D8 先读码后锚点（有效 rg 需先读调用链）。
- **去重机械化**：DEBT 已在某维度登记的，新维度只允许新增该 DEBT 未覆盖的不变点，不重复登记主因。
- **锚点可回归**：下一轮重跑锚点命中数与上轮 diff>0 必须在报告显式说明（防「代码已变锚点表未变」的伪可复现）。

## 1. 标准审计流程（deterministic，F-QA Q2）

1. 选一个维度，读本指南对应章节（四段式：A 锚点 / B 不变点 / C 门槛 / D 闭环）。
2. **A 锚点表**：跑该维度固定 rg 命令，命中数进表（机器数）。**跑完才读码**。
3. 读真实代码（不只看声明），按 **B 不变点** 逐条判定 pass/fail/n/a，带 file:line 证据。
4. 写临时证据：位置、路径、触发条件、为什么测试没覆盖。
5. **C 证据门槛**：锚点全跑、不变点逐条判定、非债确认有反例、与已审维度去重。
6. 写 `reports/<date>-<dimensions>-audit.md`（含锚点命中表 + 上轮 diff 说明）。
7. **D 闭环**：登记 `DEBT-NNN`；更新 `debt.md` 审计维度计划进度。

### 1.1 维度模板（每维度四段，D4 为完整样板）

每维度四段，**rg 命令放 `sh` 代码块、命中表只记锚点描述 + 命中数**（避免表格列被 rg 的 `|` 破坏）：

- **A. 锚点（先 rg，跑完才读码）**：一段 `sh` 代码块列编号锚点 `a1, a2, …`，每条一行 `rg …`；紧跟命中表（列：`#` / 锚点 / 本轮命中 / 上轮 diff）。命中数是机器数，进表才算跑过。
- **B. 必查不变点**：逐条 `- [ ] I1: … — 证据: path:line`，每条判定 pass / fail / n/a。
- **C. 证据门槛**（done 判据）：锚点全跑命中进表；不变点逐条判定（n/a 须说明）；非债确认须 ≥1 反例不变点；与已审维度去重。
- **D. 闭环动作**：新发现 → `DEBT-NNN`；写 `reports/<date>-<dims>-audit.md`（含命中表 + 上轮 diff）；下轮重跑 A，diff>0 须说明。

> 例外：D5（调度/迁移）、D8（测试覆盖）保留「先读码后锚点」——有效 rg 需先读调用链（GOTCHA#25/26 迁移路径），A 段标注「先读码」。

核验级别：
- ✅ 坐实：读代码 + grep/调用链足以证明。
- ⚠️ 待压测：代码可疑，但需要构造并发/压力复现。
- ❓ 假设：仅作为调查线索，不能排修复优先级。

## 2. 14 个审计维度

### D1 架构不变量

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 禁用特性（异常/RTTI/重型 std）
rg -n 'throw|try\s*\{|catch\s*\(|dynamic_cast|typeid|std::(vector|string|shared_ptr|unique_ptr|map)' kernel -g '!*test*'
# a2 ErrorOr / errno 边界
rg -n 'ErrorOr|return -E|return -1|errno' kernel -g '!*test*'
# a3 禁用头
rg -n '#include <(vector|string|memory|iostream|algorithm)>' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 禁用特性 | _ | _ |
| a2 | ErrorOr/errno | _ | _ |
| a3 | 禁用头 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: C++17、无异常、无 RTTI（a1 命中须全在 test/ 或有豁免说明）。
- [ ] I2: ErrorOr 内核内部闭环；syscall 边界翻 errno（内核深处不直接返用户 errno）。
- [ ] I3: Cinux-Base 不被复制污染；kernel/ 不重写其已有类型。
- [ ] I4: 子系统依赖方向不倒置（arch→proc→syscall 单向）。

**C. 证据门槛**

- 3 锚点全跑命中进表；a1 命中逐个确认是否豁免；ErrorOr 返回点逐个确认闭环。

**D. 闭环**

- DEBT-NNN（D1）。报告 `reports/<date>-d1-audit.md`。下轮 diff>0 说明。

### D2 内存生命周期

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 分配/释放
rg -n 'new |delete |alloc_page|free_page|alloc_pages|free_pages|kmalloc|kfree|operator new|operator delete' kernel -g '!*test*'
# a2 refcount/mapcount
rg -n 'refcount|mapcount|acquire\(|release\(|shared|CLONE_VM|FLAG_COW' kernel -g '!*test*'
# a3 释放路径
rg -n 'clear_user_mappings|free_subtree|exit_current|sys_exit|waitpid' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 分配/释放 | _ | _ |
| a2 | refcount/mapcount | _ | _ |
| a3 | 释放路径 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 每个分配有唯一 owner 或 refcount/mapcount（共享物理页须 mapcount）。
- [ ] I2: 正常/错误/退出三路径都释放（error path ≠ success path 释放规则即债）。
- [ ] I3: CoW/page table/kernel stack/Task/VMA/DMA buffer 共享对象有计数。
- [ ] I4: 退出释放 Task/地址空间/核栈（无泄漏，DEBT-002/006 系）。

**C. 证据门槛**

- 3 锚点全跑命中进表；每个分配点确认 owner/refcount；释放路径三路径对照。已审（`reports/2026-06-20-memory-smp-audit.md`），新发现只记未覆盖不变点。

**D. 闭环**

- DEBT-NNN（D2）。报告 `reports/<date>-d2-audit.md`。下轮 diff>0 说明。

### D3 SMP / 并发安全

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 全局可变状态
rg -n 'static .*\*|static .* g_|extern .* g_|bool .*waiting|registry|global|head_' kernel -g '!*test*'
# a2 同步原语
rg -n 'Spinlock|irq_guard|guard\(|Atomic|__atomic|prepare_to_wait|schedule_blocked|unblock' kernel -g '!*test*'
# a3 IRQ/IPI
rg -n 'cli|sti|irq_save|irq_restore|eoi|IPI|send_ipi' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 全局可变状态 | _ | _ |
| a2 | 同步原语 | _ | _ |
| a3 | IRQ/IPI | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 全局可变状态有锁/atomic/per-CPU 保护（裸全局链表跨核读写即债）。
- [ ] I2: 普通 bool/int 不承载跨 CPU 同步语义（wait flag 须 atomic/prepare-to-wait）。
- [ ] I3: 阻塞/唤醒无 lost-wakeup（prepare-to-wait 风格）。
- [ ] I4: IRQ 上下文不重入同一锁（irq-safe 或明确不可达）；持锁不 schedule。

**C. 证据门槛**

- 3 锚点全跑命中进表；全局状态逐个确认同步；lost-wakeup 路径逐个确认 prepare-to-wait。已审（`reports/2026-06-20-memory-smp-audit.md`）。

**D. 闭环**

- DEBT-NNN（D3）。报告 `reports/<date>-d3-audit.md`。下轮 diff>0 说明。

### D4 进程 / 线程生命周期（deterministic 范式样板）

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 状态机
rg -n 'TaskState::' kernel -g '!*test*'
# a2 退出/释放
rg -n 'exit_current|release_resources|sys_exit' kernel -g '!*test*'
# a3 reap/Zombie
rg -n 'waitpid|Zombie|reparent' kernel -g '!*test*'
# a4 共享对象 refcount
rg -n 'sig_actions|fd_table|SharedCwd|acquire\(|release\(' kernel -g '!*test*'
# a5 tid/pid 分配
rg -n 'next_tid|PidAllocator|g_pid_alloc' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 状态机 | _ | _ |
| a2 | 退出/释放 | _ | _ |
| a3 | reap/Zombie | _ | _ |
| a4 | 共享对象 refcount | _ | _ |
| a5 | tid/pid 分配 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 每个 Task 有明确 owner 或 refcount；exit 路径释放 sig_actions/fd_table/cwd/addr_space/核栈。
- [ ] I2: Zombie→reap：exit 设 Zombie，waitpid/parent 回收，无 Task 泄漏。
- [ ] I3: 线程组共享对象（CLONE_SIGHAND/FILES/FS）最后引用（group_leader exit）才释放。
- [ ] I4: children/parent/sibling 链表只被 owner task 访问（跨核裸遍历归 D3）。
- [ ] I5: next_tid/PidAllocator 跨测试复位、跨核无污染（GOTCHA#22 同类）。

**C. 证据门槛**

- 5 锚点全跑命中进表；5 不变点逐条判定；非债须反例；与 D2（内存）/D3（并发）去重（状态机/线程组/reap 是 D4 特有）。

**D. 闭环**

- 新发现 → DEBT-NNN（D4）。报告 `reports/<date>-d4-audit.md`。下轮 a1-a5 diff>0 说明。

### D5 调度 / 迁移 / CPU 上下文（例外：先读码后锚点）

> 有效 rg 需先读 context_switch/迁移调用链（GOTCHA#25/26）才能定锚点。先读码，再跑锚点。

**A. 锚点（先读码后 rg）**

```sh
# a1 per-CPU / current
rg -n 'current\(|set_current|percpu\(\)|cpu_id' kernel -g '!*test*'
# a2 context switch / FPU/TLS/GS
rg -n 'context_switch|fxsave|fxrstor|fs_base|gs_base|swapgs|tss_set_rsp0' kernel -g '!*test*'
# a3 runqueue / 抢占
rg -n 'runqueue|pick_next|add_task|dequeue|idle|yield|tick|quantum' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | per-CPU/current | _ | _ |
| a2 | ctx switch/FPU/TLS/GS | _ | _ |
| a3 | runqueue/抢占 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: current() per-CPU（非全局）。
- [ ] I2: context_switch 恢复点读 per-CPU current（非局部 next，GOTCHA#23）。
- [ ] I3: AP 上不用 BSP-only 状态；迁移后 GS/TLS/FPU 来自当前 CPU/task（GOTCHA#25/26）。
- [ ] I4: runqueue 操作原子取出/放回；时间片非全局单变量。

**C. 证据门槛**

- 先读 context_switch/迁移链，再跑 3 锚点；不变点逐条判定（迁移路径须读码确认，非仅 rg）。

**D. 闭环**

- DEBT-NNN（D5）。报告 `reports/<date>-d5-audit.md`。下轮 diff>0 说明。

### D6 用户 / 内核边界

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 用户指针访问
rg -n 'user_|is_user_addr|copy_from_user|copy_to_user|reinterpret_cast<.*\*>\(.*user' kernel -g '!*test*'
# a2 VMA 权限 / ELF 边界
rg -n 'e_phnum|p_memsz|p_filesz|VmaFlags|vma->|user_rsp|SignalFrame' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 用户指针 | _ | _ |
| a2 | VMA/ELF 边界 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 用户指针 copy 风格访问（不裸信任，至少查规范地址）。
- [ ] I2: 写用户栈/信号帧前验 VMA + 权限。
- [ ] I3: syscall 返回不泄漏内核指针/ErrorOr（翻 errno）。
- [ ] I4: ELF/偏移/长度有上限 + 溢出检查（DEBT-012 phnum，跨 D14）。

**C. 证据门槛**

- 2 锚点全跑命中进表；用户指针访问点逐个确认 copy 封装；VMA 校验点逐个确认。

**D. 闭环**

- DEBT-NNN（D6）。报告 `reports/<date>-d6-audit.md`。下轮 diff>0 说明。

### D7 错误处理 / 崩溃韧性

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 panic/assert/OOM
rg -n 'kpanic|panic|ASSERT|OOM|OutOfMemory|return nullptr' kernel -g '!*test*'
# a2 诊断路径
rg -n 'kprintf|klog_|dump_|backtrace|dump_memory_stats' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | panic/assert/OOM | _ | _ |
| a2 | 诊断路径 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: panic 仅用于不变量破坏（可恢复外部输入错误走 ErrorOr/errno）。
- [ ] I2: OOM 可诊断，不静默半初始化后继续。
- [ ] I3: panic/backtrace 路径不依赖危险锁/堆分配（避免递归崩）。
- [ ] I4: 日志不用 kprintf 不支持的格式（如 %zu，FO GOTCHA）。

**C. 证据门槛**

- 2 锚点全跑命中进表；panic 点逐个确认是否真不变量；OOM 路径逐个确认诊断。

**D. 闭环**

- DEBT-NNN（D7）。报告 `reports/<date>-d7-audit.md`。下轮 diff>0 说明。

### D8 测试覆盖盲区（例外：先读码后锚点）

> 有效 rg 需先读测试入口/调用链才能判断覆盖。先读码，再跑锚点。

**A. 锚点（先读码后 rg）**

```sh
# a1 测试入口
rg -n 'RUN_TEST|TEST_SECTION|run_.*tests|test_' kernel/test test/unit
# a2 覆盖缺口标记
rg -n 'TODO.*test|not covered|smoke|manual|xfail|GOTCHA' kernel/test test/unit
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 测试入口 | _ | _ |
| a2 | 覆盖缺口 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 新行为在 run-kernel-test 真执行（非仅 host 单测）。
- [ ] I2: host mock 与真内核接口一致（InodeOps/FDTable 签名同步）。
- [ ] I3: SMP/用户态/设备路径不只 GUI 冒烟（有断言或说明）。
- [ ] I4: 并发 bug 修复有交错测试或压力计划（非仅单核绿）。

**C. 证据门槛**

- 先读测试 main/调用链，再跑 2 锚点；新行为逐个确认有测试；SMP/用户态路径逐个确认覆盖方式。

**D. 闭环**

- DEBT-NNN（D8）。报告 `reports/<date>-d8-audit.md`。下轮 diff>0 说明。

### D9 静态 / 动态检查工具

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 工具/sanitizer 开关
rg -n 'CINUX_UBSAN|CINUX_LOCKDEP|CINUX_HOST_ASAN|Werror|clang-tidy' CMakeLists.txt kernel test .github
# a2 布局/属性断言
rg -n 'static_assert|NotNull|__attribute__|nodiscard|noreturn' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 工具开关 | _ | _ |
| a2 | 布局/属性 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 警告 flag 零警告基线保持（-Werror= 子项）。
- [ ] I2: 新 printf-like 函数有 format 属性；新布局结构有 static_assert。
- [ ] I3: UBSAN/lockdep/host-ASAN opt-in 可构建 + 零命中。
- [ ] I4: 新锁类型不绕过 lockdep。

**C. 证据门槛**

- 2 锚点全跑命中进表；新函数/结构逐个确认属性/断言；sanitizer 构建实测零命中。

**D. 闭环**

- DEBT-NNN（D9）。报告 `reports/<date>-d9-audit.md`。下轮 diff>0 说明。

### D10 文档 / 可追溯性

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 TODO/债务标记
rg -n 'GOTCHA|DEBT-|TODO|FIXME|XXX|follow-up|推迟' kernel document
# a2 文档引用
rg -n 'PLAN|ROADMAP|notes|DEBT' document/ai
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | TODO/债务 | _ | _ |
| a2 | 文档引用 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: PLAN/ROADMAP/todo/notes/DEBT 同步（代码改 PLAN 指向更新）。
- [ ] I2: GOTCHA 写在后人会读的位置（PLAN OPEN GOTCHAS 或 notes）。
- [ ] I3: 注释解释 why（非复述代码）；设计文档记不做什么。
- [ ] I4: 债务修了 debt.md 闭环（标 ✅ + commit，不删条目）。

**C. 证据门槛**

- 2 锚点全跑命中进表；TODO 逐个确认是否过期；文档引用逐个确认同步。

**D. 闭环**

- DEBT-NNN（D10）。报告 `reports/<date>-d10-audit.md`。下轮 diff>0 说明。

### D11 模块组织 / 可维护性

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 文件行数（CI check_line_limits.py 硬门禁 500）
wc -l kernel/**/*.cpp kernel/**/*.hpp 2>/dev/null | sort -n | tail -20
# a2 重复/分层
rg -n 'static .*helper|TODO.*split|duplicate|copy of|same as' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 文件行数 | _ | _ |
| a2 | 重复/分层 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 源文件 ≤500 行软上限（CI 硬门禁）。
- [ ] I2: 模块职责聚焦（一个文件不混 syscall+状态机+释放+测试桩）。
- [ ] I3: 无重复实现/ad-hoc helper（复用 Cinux-Base/既有）。
- [ ] I4: 头依赖不反向（公共 helper 放对层）。

**C. 证据门槛**

- 2 锚点全跑命中进表；超 500 行文件逐个列；重复点逐个确认。

**D. 闭环**

- DEBT-NNN（D11）。报告 `reports/<date>-d11-audit.md`。下轮 diff>0 说明。

### D12 发布 / 回归 / 变更管理

**A. 锚点（先 git，跑完才读码）**

```sh
# a1 最近提交
git log --oneline -15
# a2 工作树/差异
git status --short && git --no-pager diff --stat
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 最近提交 | _ | _ |
| a2 | 工作树 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 一批一 commit 一验证（绿才提交）。
- [ ] I2: commit msg 纯描述（`<type>(<scope>): 中文`），无 AI trailer。
- [ ] I3: 一个 commit 不混多个无关风险域。
- [ ] I4: 高危改动有额外验证矩阵（-smp 2/UBSAN/LOCKDEP，QUALITY-GATES §4）。

**C. 证据门槛**

- 2 锚点全跑命中进表；最近提交逐个确认一 commit 一域；红提交零容忍。

**D. 闭环**

- DEBT-NNN（D12）。报告 `reports/<date>-d12-audit.md`。下轮 diff>0 说明。

### D13 资源配额 / 非堆边界

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 上限常量
rg -n 'PID_MAX|FD_TABLE_SIZE|MAX_WINDOWS|kMaxCpus|PIPE_BUFFER_SIZE|PATH_MAX|USER_BRK_MAX|MOUNT_PATH_MAX' kernel -g '*.hpp'
# a2 分配点
rg -n 'g_pmm\.alloc_page|kmalloc|cache_alloc|FDTable::alloc|PidAllocator::alloc' kernel -g '!*test*'
# a3 上限检查
rg -n 'full\(\)|>= .*MAX|< .*MAX|>= .*_SIZE' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | 上限常量 | _ | _ |
| a2 | 分配点 | _ | _ |
| a3 | 上限检查 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 每个分配点（fd/pid/window/page）有上限检查（满则拒，非无界）。
- [ ] I2: 上限常量全局一致（同名不同值即债，如 `kMaxCpus`）。
- [ ] I3: 大对象/缓冲不上内核栈（16KB 栈，参考 DEBT-015）。
- [ ] I4: 用户资源配额（fd/内存/进程数）有封顶，防 DoS。

**C. 证据门槛**

- 3 锚点全跑；上限常量逐个列值进表（查不一致）；分配点逐个确认有上限。

**D. 闭环**

- DEBT-NNN（D13）。⚠️ 已知线索：`kMaxCpus` percpu.hpp=8 vs acpi.hpp=16（Q3 首审候选）。

### D14 整数溢出 / 边界

**A. 锚点（先 rg，跑完才读码）**

```sh
# a1 size 算术
rg -n 'static_cast<size_t>|static_cast<uint64_t>|count - |offset \+ |size \*' kernel -g '!*test*'
# a2 用户可控长度（ELF 段）
rg -n 'e_phnum|p_memsz|p_filesz' kernel -g '!*test*'
# a3 数组索引
rg -n '\[[a-z_]+\]' kernel -g '!*test*'
```

| # | 锚点 | 本轮命中 | 上轮 diff |
|---|------|---------|-----------|
| a1 | size 算术 | _ | _ |
| a2 | 用户可控长度 | _ | _ |
| a3 | 数组索引 | _ | _ |

**B. 必查不变点（逐条 pass/fail/n/a + file:line）**

- [ ] I1: 用户可控 count 参与乘法有溢出检查（如 `count * size` 上限）。
- [ ] I2: 长度/偏移/段数有上限（DEBT-012 `e_phnum` 无上限 → 3.6MB 分配）。
- [ ] I3: size_t/uint64 转换无损（无窄化溢出）。
- [ ] I4: 数组索引有界检查（用户/计算值不裸索引）。

**C. 证据门槛**

- 3 锚点全跑；用户可控整数逐个确认上限/溢出检查；窄化转换逐个确认安全。

**D. 闭环**

- DEBT-NNN（D14）。已知：DEBT-012（e_phnum 无上限）归此维度。

## 3. 债务优先级判定

| 优先级 | 判据 | 处理方式 |
|--------|------|----------|
| P0 | 已知可导致 UAF、数据损坏、死锁、随机挂死 | 下一专项修复候选 |
| P1 | 长期运行泄漏、SMP 竞态、错误对象引用 | 纳入近期里程碑 |
| P2 | 加固、一致性、未来特性会触发 | 排入 follow-up |
| P3 | 风格、边角防御、低概率 | 登记即可，触碰时修 |

## 4. 登记模板

```md
### DEBT-NNN 标题
- **维度**: D? 名称　**优先级**: P?　**状态**: 🆕 登记待办　**核验**: ✅/⚠️/❓
- **位置**: `path:line`
- **现象**:
- **根因**:
- **触发条件**:
- **修复建议**:
- **验证建议**:
- **关联 GOTCHA**:
```

## 5. 修复专项拆批建议

适合一批一 commit 的粒度：
- 一个全局状态加锁 + 对应测试。
- 一个 refcount/mapcount 基建 + 一个调用方接入。
- 一个 syscall 用户边界封装 + 相关 syscall 迁移。
- 一个 panic/OOM 路径加固 + 冒烟验证。

不适合一批：
- 同时改 scheduler、MM、signal、VFS。
- “顺手”清理所有 TODO。
- 无测试地重写生命周期核心。

## 6. 外部参考

Linux 官方文档可作为审计灵感，不直接照搬：
- Development process: https://docs.kernel.org/process/development-process.html
- Testing guide: https://docs.kernel.org/dev-tools/testing-overview.html
- Submit checklist: https://docs.kernel.org/process/submit-checklist.html
- Lockdep: https://docs.kernel.org/locking/lockdep-design.html
- KASAN/UBSAN/kmemleak/KCSAN/Sparse/Coccinelle: https://docs.kernel.org/dev-tools/index.html
