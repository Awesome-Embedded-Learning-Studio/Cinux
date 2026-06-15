# CinuxOS — Claude Code 工作指引

C++17 内核，13 Feature / ~60 Milestone 长弧。Claude 是长期主力开发。

## 文档分层（按耐久度，按需读）
- `document/ai/DIRECTIVES.md` — 架构铁律 + 约定 + 操作模型（年，最稳）
- `document/ai/ROADMAP.md` — 里程碑全树 + 状态（里程碑级）
- `document/ai/PLAN.md` — 当前焦点批级进度（批级，最易变）
- `document/ai/CODING-TASTE.md` — 编码/注释风格权威（写代码前读）
- `document/ai/DEVLOG.md` — 开发日志（编年工作记录，最新在最上）

## 始终遵守（每条便宜，违规代价大）
- 无异常：错误经 `cinux::lib::ErrorOr<T>`（Cinux-Base 子模块，勿在 kernel/ 重写）。
- 验证只用 `cmake --build build --target run-kernel-test -j$(nproc)`（~662 项，14 核并行）；`run`/`test_host`/`make run` 非验证。
- 一批一 commit 一验证；绿才提交。
- 提交信息 `<type>(<scope>): <中文简述>`（纯描述变更；里程碑归属看 PLAN.md，不入 msg），**不带 Co-Authored-By / AI 署名**。
- 改前查牵连（grep 引用方），同步 ROADMAP↔PLAN↔document/todo↔git。
- 新里程碑/跨子系统大改：propose-then-execute。
- 写代码遵循 `document/ai/CODING-TASTE.md`：标识符+注释一律英文、私有成员 `_`、常量/枚举值 `k`前缀（目标）、提交前跑 clang-format。

## 命令（`.claude/commands/`）
战术：`/resume` `/status` `/next [批]` `/done`
战略：`/roadmap` `/milestone [M]` `/audit`

## 回到仓库
`/resume`（读 PLAN.md + git log）。Codex 等价粘贴 prompt 见 `document/ai/prompts.md`（`AGENTS.md` 自洽）。
