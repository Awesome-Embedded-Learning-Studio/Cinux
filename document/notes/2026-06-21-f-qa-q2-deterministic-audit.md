# F-QA Q2：deterministic 审计方法论改造

> 里程碑：F-QA（质量收敛与加固）Q2。分支 `feat/f-qa-q2`（叠 DEBT-017 `a93940d`）。2026-06-21。
> 防新债的方法论基座：audit-guide 叙述式 → per-维度 deterministic 四段式，两轮审计可比较（根治发散）。

## 背景

交接文档（`2026-06-20-f-qa-handoff.md`）定 Q2：把 `audit-guide.md` 从「看什么/常用搜索/红线」叙述式改造成 per-维度 deterministic 锚点 checklist。前轮 6-agent workflow 的 critic 掀翻最大错误：D13/D14 锚点用了虚构符号（`alloc_fd`/`request_irq` 零命中），真实是 `FDTable::alloc`/`PidAllocator::alloc`。用户决策「2 批连续做完」。

## 范式（每维度四段，D4 为样板）

- **A 锚点**（先 rg，跑完才读码）：`sh` 代码块列编号锚点 a1/a2/…（每条 `rg …`）+ 命中表（#/锚点/本轮命中/上轮 diff）。命中数机器数，进表才算跑过。
- **B 必查不变点**：逐条 `- [ ] I1: … — 证据: path:line`，pass/fail/n/a。
- **C 证据门槛**（done 判据）：锚点全跑命中进表；不变点逐条判定（n/a 须说明）；非债须 ≥1 反例；与已审维度去重。
- **D 闭环**：DEBT-NNN → `reports/<date>-<dims>-audit.md`（含命中表 + 上轮 diff）；下轮重跑 A，diff>0 须说明。

**例外**：D5（调度/迁移）、D8（测试覆盖）保留「先读码后锚点」——有效 rg 需先读调用链（GOTCHA#25/26 迁移路径）。

## 2 批

- **批1**（`37a1332`）：范式骨架（§0 deterministic 原则 + §1 四段流程 + §1.1 模板）+ D4 完整样板 + D13/D14 新增（真实符号）。
- **批2**（本次）：D1-D3/D5-D12 四段式（D5/D8 标例外）。

## 关键校正

- **rg 语法**：初版锚点表把 rg 命令放表格单元格，`|`（rg 交替）破坏 markdown 列。改：rg 移 `sh` 代码块，命中表只留锚点描述 + 命中数。同时修正 `\|`（grep BRE 习惯）→ `|`（rg Rust regex 交替）+ `\\s`（旧版 markdown 转义误解，```sh 块不转义）→ `\s`。
- **符号校准**（避免虚构）：D13 `PID_MAX`/`FD_TABLE_SIZE`/`MAX_WINDOWS`/`kMaxCpus`/`PIPE_BUFFER_SIZE` + `g_pmm.alloc_page`/`kmalloc`/`FDTable::alloc`/`PidAllocator::alloc`；D14 `static_cast<size_t>`/`e_phnum`/`p_memsz`。全部 rg 校准命中>0。
- **顺手坐实**：`kMaxCpus` 不一致（acpi.hpp `size_t=16` vs percpu.hpp `uint32_t=8`，类型也不同）→ D13 I2 真线索，Q3 首审即抓。

## 防注水机制（§0 + 每维度 C 段）

- 命中数机器数（非 yes/no / 非「≥5 非债」凑数）。
- 非债确认须 ≥1 反例不变点（非「看起来 OK」）。
- DEBT 去重机械化（已登记只补未覆盖不变点，非「判断主因」）。
- 锚点可回归（下轮 diff>0 须报告说明，防代码变锚点不变的伪可复现）。

## 验证

文档-only（R0，QUALITY-GATES §4 文档-only 可不跑内核）。自检：

- 14 维度齐全（D1-D14，D5/D8 标例外）。
- 叙述式残留 0（无「看什么/常用搜索/红线」）。
- 抽样锚点全可跑：D4 a1-a5（29/43/50/139/34）、D6（52）、D7（88）、D9（36）、D10（353）、D13（27/65/59）、D14（89/571）。
- audit-guide 554 行（文档，不受源文件 500 限制）。

## 改动清单

- `document/todo/quality/audit-guide.md`：全 14 维度四段式 + §0/§1 范式 + §1.1 模板（批1+批2）。
- `document/todo/quality/debt.md`：审计维度计划 12→14（D13/D14 新增）+ 进度 2/14。
- `document/ai/PLAN.md`：F-QA Q2 ✅ 收官段。

## 下个

Q3 系统审计（用 Q2 方法论审待审维度 D4/D5/D6/D7/D11 + 新 D13/D14；零风险只读）→ 喂 Q4（修头号高危债）。
