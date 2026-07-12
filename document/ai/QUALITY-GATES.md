# Cinux Quality Gates — AI 预审 / 审查 / 修复流程

> Tier 1.5（工程流程，稳定但可演进）。本文定义每轮代码变更前后必须回答的问题，让本地 Claude Code / Codex 能按清单预审、审查、修复，而不是只靠经验。
>
> 事实源关系：`DIRECTIVES.md` = 架构铁律；`CODING-TASTE.md` = 代码风格；`document/todo/quality/debt.md` = 债务登记；本文 = 每轮执行门禁；`document/todo/quality/audit-guide.md` = 深度审计方法。

## 0. 使用模式

### A. 预审（改代码前）

目标：判断这次改动属于哪些风险域，提前列出必须检查的不变量。

Claude Code 必须输出：
- **范围**：本轮要改什么，不改什么。
- **触及文件**：用 `rg`/`git grep` 找调用方、共享状态、生命周期边界。
- **风险域**：从本文第 2 节选择。
- **必须验证**：最小测试 + 额外矩阵。
- **文档同步**：是否需要 PLAN/ROADMAP/DEBT/notes/todo。

### B. 审查（提交前）

目标：确认改动没有破坏架构、生命周期、SMP、用户边界和文档一致性。

Claude Code 必须输出：
- **发现**：按严重性列 file:line，先 bug 后风格。
- **门禁结果**：第 3 节逐项 pass/fail/n/a。
- **验证结果**：命令、测试数、失败项。
- **提交状态**：绿才允许建议 commit message。

### C. 专项修复（处理 `DEBT-NNN`）

目标：一条债务一批闭环，避免“顺手修一大片”造成新不确定性。

修复流程：
1. 读 `document/todo/quality/debt.md` 对应条目，确认位置、根因、建议。
2. `rg` 查所有引用方和相邻不变量。
3. 写小设计：owner、同步策略、错误路径、测试计划。
4. 改代码 + 测试。
5. 更新 `document/todo/quality/debt.md` 状态，写 `document/notes/<date>-<topic>.md`。
6. 绿才提交。

## 1. 风险等级

| 等级 | 判据 | 门禁强度 |
|------|------|----------|
| R0 | 文档/注释/只读说明 | 不跑内核测试也可，但需说明未跑原因 |
| R1 | 局部函数、无共享状态、无 ABI | `run-kernel-test` |
| R2 | 改公共接口、VFS/FS/driver mock、CMake | `run-kernel-test` + 全量 build 或 host test |
| R3 | MM/PMM/VMM/slab/page table/VMA/CoW | R2 + OOM/边界/释放路径测试 |
| R4 | scheduler/process/signal/futex/wait/clone/SMP | R2 + `-smp 2` 相关测试/冒烟 + lockdep 可行时打开 |
| R5 | interrupt/APIC/syscall/userspace ABI/device DMA | R2 + 真机/QEMU 冒烟；必要时串口日志留证 |

## 2. 风险域映射

| 改动位置/主题 | 必查风险域 |
|---------------|------------|
| `kernel/mm/`、`AddressSpace`、页表、VMA | 内存生命周期、权限、TLB/映射属性、OOM |
| `kernel/proc/`、`kernel/syscall/sys_exit.cpp` | 进程生命周期、SMP、状态机、父子/线程组 |
| `scheduler`、`roundrobin`、`context_switch` | per-CPU、迁移、FPU/TLS/GS、抢占点 |
| `signal`、`futex`、`waitpid`、`mutex`、`semaphore` | lost-wakeup、原子性、阻塞协议、signal interruption |
| `syscall` 新增/改签名 | 用户指针、errno 翻译、ABI、VMA 权限 |
| `drivers`、DMA、MMIO、APIC | cache 属性、物理地址、IRQ/EOI、错误恢复 |
| `fs`、`vfs`、`ext2`、page cache | lifetime、引用计数、缓存一致性、host mock |
| `third_party/Cinux-Base` | 子模块边界、header-only、无堆、≤400 行 |
| CMake/CI/tooling | 本地/CI 对等、timeout、防挂死、产物路径 |

## 3. 提交前门禁

### G0 上下文门

- [ ] 已读 `PLAN.md` 当前焦点和最近 git log。
- [ ] 知道本轮是否属于新里程碑/跨子系统大改；若是，先 propose。
- [ ] 未覆盖用户未提交改动；`git status --short` 已核对。

### G1 范围门

- [ ] 本轮范围能用一句话描述。
- [ ] 非目标改动没有混入。
- [ ] 公共接口改动已 grep 所有调用方。

### G2 架构铁律门

- [ ] C++17，无异常、无 RTTI。
- [ ] 内核内部错误走 `ErrorOr`；syscall 边界翻 errno。
- [ ] Cinux-Base 不引入堆、OS 依赖或重型标准库。
- [ ] 不在 `kernel/` 重写 Cinux-Base 已有类型。

### G3 生命周期门

- [ ] 新增 owner 时写清谁释放。
- [ ] 共享对象有 refcount/mapcount/锁保护。
- [ ] error path 与正常 path 都释放资源。
- [ ] move/copy 语义明确；禁止隐式双 owner。
- [ ] 对页、栈、Task、VMA、Inode、File、DMA buffer 做了释放路径审查。

### G4 SMP / 同步门

- [ ] 新增或触碰全局可变状态时写明同步策略。
- [ ] 跨 CPU 读写使用锁或 atomic；普通 bool/int 不承载同步语义。
- [ ] 阻塞协议使用 prepare-to-wait 风格，避免 check-then-sleep lost-wakeup。
- [ ] IRQ 可能触达的锁用 irq-safe 形式或明确不可达。
- [ ] 持锁不 schedule；lockdep 可覆盖时打开验证。
- [ ] memory barrier / acquire-release 必须有注释解释“为什么”。

### G5 用户边界门

- [ ] 用户指针不直接信任；至少检查规范地址，优先 `copy_*_user` 风格封装。
- [ ] 写用户栈/信号帧前验证 VMA 与权限。
- [ ] syscall 返回值不泄漏内核指针或 `ErrorOr`。
- [ ] ELF、文件偏移、长度、计数有上限与溢出检查。

### G6 错误与崩溃门

- [ ] `panic` 用于不变量破坏；可恢复错误返回 `ErrorOr`/errno。
- [ ] OOM 路径可诊断，不静默半初始化。
- [ ] panic/backtrace 路径不递归依赖危险锁或堆分配。
- [ ] 日志不使用 kprintf 不支持的格式。

### G7 测试门

- [ ] 基础验证**默认 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`**（F-VERIFY：单核 → -smp 2 一条命令，防忘跑 -smp 变体）；单 leg 调试用 `timeout 40 ... run-kernel-test`。
- [ ] 改公共接口/host mock 相关代码时补全量 build 或 host test。
- [ ] 改 SMP/调度/中断时补 `-smp 2` 相关验证或说明不可跑原因。
- [ ] 新行为有测试；无法测试的路径写明人工冒烟和日志证据。
- [ ] **串口日志读法**：QEMU serial.log 带 ANSI escape，手动 `grep` 须加 `-a`
      （否则只报 `binary file matches`，GOTCHA#2 复发 3+ 次）；脚本侧
      `check_test_count.sh` 已默认 `grep -a`。kprintf `%p` 已自带 `0x` 前缀，
      调用处写 `0x%p` 会输出 `0x0x...`——用 `%p` 即可。

### G8 文档门

- [ ] 完成批次更新 `PLAN.md`；里程碑级变化同步 `ROADMAP.md`/`document/todo`。
- [ ] 修债务更新 `document/todo/quality/debt.md` 状态，保留原条目和 commit 指针。
- [ ] 每批写 `document/notes/<date>-<topic>.md`，记录背景、设计、陷阱、验证。
- [ ] 新 GOTCHA 写入 PLAN 或对应 notes，防复发。

## 4. 验证矩阵

| 改动类型 | 必跑 | 条件加跑 |
|----------|------|----------|
| 文档-only | 可不跑；需说明 | `markdown`/链接人工检查 |
| 普通内核代码 | `run-kernel-test` | 全量 build |
| 公共接口/host mock | `run-kernel-test` | `cmake --build build -j$(nproc)` 或 `make test_host` |
| MM/生命周期 | `run-kernel-test` | OOM/释放/重复 fork-exec 压力 |
| SMP/调度/锁 | `run-kernel-test` | `run-kernel-test-smp`、`CINUX_LOCKDEP=ON` |
| 设备/中断 | `run-kernel-test` | QEMU 真机冒烟、串口日志 |
| UBSAN/lockdep/tooling | 默认配置测试 | 对应 opt-in 配置测试 |

## 5. Claude Code 输出模板

### 预审模板

```text
范围：
触及文件：
风险域：
必须不变量：
验证计划：
文档同步：
停等确认/可以执行：
```

### 审查模板

```text
Findings:
- [severity] file:line — issue

Gate results:
G0 context: pass/fail/n/a
...

Verification:
- command: result

Residual risk:
```

### 修债模板

```text
Debt:
Root cause:
Design:
Files:
Tests:
Docs:
Commit message:
```

## 6. 参考基线

本流程对齐 Linux 的工程分层，但按 Cinux 当前体量裁剪：
- Development process: https://docs.kernel.org/process/development-process.html
- Kernel testing guide: https://docs.kernel.org/dev-tools/testing-overview.html
- Submit checklist: https://docs.kernel.org/process/submit-checklist.html
- Runtime tools: KASAN / UBSAN / kmemleak / KCSAN / lockdep / Sparse / Coccinelle
