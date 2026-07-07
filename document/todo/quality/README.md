# Quality: 基建质量与审计

> 横切质量域。这里存放长期质量债、审计维度进度、每轮审计报告和专项修复入口。`document/ai/` 只保留 AI 执行协议；真实审计产物沉淀在本目录。

## 文件清单

| 文件 | 角色 |
|------|------|
| [debt.md](debt.md) | 稳定债务登记表，按 `DEBT-NNN` 跟踪到闭环 |
| [audit-guide.md](audit-guide.md) | 深度审计方法，定义 D1-D12 维度与登记模板 |
| [reports/](reports/) | 每轮审计的一次性报告；报告不反复改写，只追加新轮次 |

## 分层规则

- `document/ai/QUALITY-GATES.md`：每轮提交怎么预审、审查、修债。
- `document/ai/prompts.md`：给 Claude Code / Codex 的粘贴式入口。
- `document/todo/quality/audit-guide.md`：审计怎么做。
- `document/todo/quality/debt.md`：最终登记哪些债务。
- `document/todo/quality/reports/*.md`：每轮审计看了什么、发现了什么、哪些不是债。

## 审计报告规则

每轮审计必须产出一篇报告：

```text
document/todo/quality/reports/<date>-<dimensions>-audit.md
```

报告必须包含：
- 范围：审计了哪些维度，明确不审什么。
- 方法：读了哪些代码区域，使用了哪些 `rg` 搜索。
- 发现：新增/确认的 `DEBT-NNN` 列表。
- 非债确认：避免后续重复报告。
- 后续建议：哪些债务适合下一批修。

报告是审计事实记录；`debt.md` 是可执行 backlog。报告可以长，`debt.md` 要稳定、可排期。

## 审计维度状态

| # | 维度 | 状态 | 当前报告 |
|---|------|------|----------|
| D1 | 架构不变量 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D2 | 内存生命周期 | ✅ 已审 2026-06-20 | [2026-06-20-memory-smp-audit.md](reports/2026-06-20-memory-smp-audit.md) |
| D3 | SMP / 并发安全 | ✅ 已审 2026-06-20 | [2026-06-20-memory-smp-audit.md](reports/2026-06-20-memory-smp-audit.md) |
| D4 | 进程 / 线程生命周期 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D5 | 调度 / 迁移 / CPU 上下文 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D6 | 用户 / 内核边界 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D7 | 错误处理 / 崩溃韧性 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D8 | 测试覆盖盲区 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D9 | 静态 / 动态检查工具 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D10 | 文档 / 可追溯性 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D11 | 模块组织 / 可维护性 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |
| D12 | 发布 / 回归 / 变更管理 | ✅ 已审 2026-06-21 (F-QA Q3) | [debt.md](debt.md) 权威 |

## 当前收敛建议

优先把 D2/D3 交叉印证出的 P0/P1 债务收敛成一个「进程生命周期与引用计数」专项：
- `DEBT-001` task registry 加锁。
- `DEBT-002` exit cleanup。
- `DEBT-003` CoW mapcount。
- `DEBT-004` waitpid wakeup 去 stale bool。
- `DEBT-005` PidAllocator 加锁。
- `DEBT-006` AddressSpace refcount。
