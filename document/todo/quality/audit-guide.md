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

看什么：
- C++17、无异常、无 RTTI。
- `ErrorOr` 是否在内核内部闭环；syscall 是否翻译 errno。
- Cinux-Base 是否被错误复制或污染。
- 子系统依赖方向是否倒置。

常用搜索：
```sh
rg -n "throw|try\\s*\\{|catch\\s*\\(|dynamic_cast|typeid|std::(vector|string|shared_ptr|unique_ptr|map)"
rg -n "ErrorOr|return -E|return -1|errno"
rg -n "#include <(vector|string|memory|iostream|algorithm)>"
```

红线：
- 内核深处直接返回用户 errno。
- syscall 之外裸 `-1` 代表错误。
- 在 `kernel/` 重新实现 Cinux-Base 已有类型。

### D2 内存生命周期

看什么：
- 每个分配是否有唯一 owner 或 refcount/mapcount。
- 正常路径、错误路径、退出路径是否都释放。
- CoW、page table、kernel stack、Task、VMA、DMA buffer 是否有共享计数。

常用搜索：
```sh
rg -n "new |delete |alloc_page|free_page|alloc_pages|free_pages|kmalloc|kfree|operator new|operator delete"
rg -n "refcount|mapcount|acquire\\(|release\\(|shared|CLONE_VM|FLAG_COW"
rg -n "clear_user_mappings|free_subtree|exit_current|sys_exit|waitpid"
```

红线：
- 共享物理页没有 mapcount。
- `Task`/地址空间/核栈正常退出不释放。
- error path 与 success path 使用不同释放规则。

### D3 SMP / 并发安全

看什么：
- 全局可变状态是否有锁/atomic/per-CPU。
- 普通 bool/int 是否跨 CPU 通信。
- 阻塞和唤醒是否存在 lost-wakeup。
- IRQ 上下文是否可能重入同一锁。

常用搜索：
```sh
rg -n "static .*\\*|static .* g_|extern .* g_|bool .*waiting|registry|global|head_"
rg -n "Spinlock|irq_guard|guard\\(|Atomic|__atomic|prepare_to_wait|schedule_blocked|unblock"
rg -n "cli|sti|interrupt|irq|eoi|IPI|smp"
```

红线：
- 裸全局链表跨核读写。
- wait flag 普通 bool 决定是否唤醒。
- 持锁进入 `schedule()`。

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

### D5 调度 / 迁移 / CPU 上下文

看什么：
- `current()` 是否 per-CPU。
- context switch 恢复点是否读正确 task。
- FPU/TLS/GS/TSS 是否随 CPU 和 task 正确切换。
- runqueue 操作是否原子取出/放回。

常用搜索：
```sh
rg -n "current\\(|set_current|context_switch|fxsave|fxrstor|fs_base|gs|swapgs|tss|rsp0"
rg -n "runqueue|pick_next|add_task|dequeue|idle|yield|tick|quantum"
```

红线：
- AP 上使用 BSP-only 状态。
- 任务迁移后 GS/TLS/FPU 状态来自旧 CPU。
- 时间片是全局单变量。

### D6 用户 / 内核边界

看什么：
- 用户指针是否 copy 风格访问。
- VMA 权限是否参与 PF/syscall/signal frame 判断。
- ELF/program header/长度/偏移是否有边界。
- 用户可控整数是否溢出。

常用搜索：
```sh
rg -n "user_|is_user_addr|copy_from_user|copy_to_user|reinterpret_cast<.*\\*>\\(.*user|\\*.*user"
rg -n "e_phnum|p_memsz|p_filesz|offset|length|count|SignalFrame|user_rsp"
```

红线：
- 只查 canonical address 就直接解引用。
- 写用户栈不查 VMA。
- 用户控制 count 参与乘法无溢出检查。

### D7 错误处理 / 崩溃韧性

看什么：
- `panic` 是否只用于不变量破坏。
- OOM 是否可诊断。
- backtrace/panic 路径是否避免堆分配和危险锁。
- 错误日志是否足够定位。

常用搜索：
```sh
rg -n "kpanic|panic|assert|OOM|OutOfMemory|return false|return nullptr|TODO.*error"
rg -n "kprintf|klog_|dump_|backtrace"
```

红线：
- 可恢复外部输入错误触发 panic。
- OOM 半初始化后继续执行。
- panic handler 依赖可能损坏的映射/锁。

### D8 测试覆盖盲区

看什么：
- 新行为是否在 `run-kernel-test` 中真的执行。
- host mock 是否与真内核接口一致。
- SMP/用户态/设备路径是否只有 GUI 冒烟。
- 是否有负例、边界、错误路径测试。

常用搜索：
```sh
rg -n "RUN_TEST|TEST_SECTION|run_.*tests|test_.*" kernel/test test/unit
rg -n "TODO.*test|not covered|smoke|manual|xfail"
```

红线：
- 启动路径改动但只跑纯单测。
- SMP 问题只跑单核。
- 修复并发 bug 没有构造交错测试或压力计划。

### D9 静态 / 动态检查工具

看什么：
- warning flags 是否仍零警告。
- clang-tidy 是否有 advisory 覆盖。
- UBSAN/lockdep opt-in 是否可构建。
- 是否需要新增脚本化 grep 门禁。

常用搜索：
```sh
rg -n "CINUX_UBSAN|CINUX_LOCKDEP|clang-tidy|Werror|Wall|Wextra|format\\("
rg -n "static_assert|NotNull|__attribute__|nodiscard|noreturn"
```

红线：
- 新 printf-like 函数没有 format 属性。
- 新结构体依赖二进制布局但无 static_assert。
- 新锁类型绕过 lockdep。

### D10 文档 / 可追溯性

看什么：
- PLAN/ROADMAP/todo/notes/DEBT 是否同步。
- GOTCHA 是否写在后人会读的位置。
- 注释是否解释 why，而不是复述代码。
- 设计文档是否记录不做什么。

常用搜索：
```sh
rg -n "GOTCHA|DEBT-|TODO|FIXME|XXX|follow-up|defer|推迟|留"
rg -n "M[0-9]|F[0-9]|PLAN|ROADMAP|notes"
```

红线：
- 代码已改，PLAN 仍指向旧焦点。
- 债务修了但 `debt.md` 未闭环。
- 注释和真实行为相反。

### D11 模块组织 / 可维护性

看什么：
- 文件是否超过软上限。
- 模块职责是否发散。
- 是否有重复实现或 ad-hoc helper。
- 头文件是否过度 include。

常用搜索：
```sh
wc -l kernel/**/*.cpp kernel/**/*.hpp 2>/dev/null
rg -n "static .*helper|TODO.*split|duplicate|copy of|same as"
```

红线：
- 一个文件同时处理 syscall、状态机、内存释放、测试桩。
- 新公共 helper 放错层，导致反向依赖。

### D12 发布 / 回归 / 变更管理

看什么：
- 每批是否一 commit 一验证。
- commit message 是否纯描述、无 AI trailer。
- 是否有回滚点和残余风险说明。
- 高危改动是否有额外验证矩阵。

常用搜索：
```sh
git log --oneline -15
git status --short
git --no-pager diff --stat
```

红线：
- 红测试提交。
- 一个 commit 混入多个无关风险域。
- 文档声称完成但 commit/测试为空。

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
