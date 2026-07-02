# CinuxOS — Claude Code 工作指引

C++17 内核，13 Feature / ~60 Milestone 长弧。Claude 是长期主力开发。

## 文档分层（按耐久度，按需读）
- `document/ai/DIRECTIVES.md` — 架构铁律 + 约定 + 操作模型（年，最稳）
- `document/ai/ROADMAP.md` — 里程碑全树 + 状态（里程碑级）
- `document/ai/PLAN.md` — 当前焦点批级进度（批级，最易变）
- `document/ai/CODING-TASTE.md` — 编码/注释风格权威（写代码前读）
- `document/notes/` — 工作记录（正式发布文档，`<date>-<milestone>-<batch>.md`；**完成一批立即补一篇**，不堆到里程碑末尾，参考既有 notes 风格；2026-06-16 起 DEVLOG 不再用）

## 始终遵守（每条便宜，违规代价大）
- 无异常：错误经 `cinux::lib::ErrorOr<T>`（Cinux-Base 子模块，勿在 kernel/ 重写）。
- **验证默认用 `cmake --build build --target run-kernel-test-all -j$(nproc)`**（F-VERIFY：一条命令顺序跑 **单核 → -smp 2** 两套，防"忘跑 -smp 变体"；两条 leg，`timeout` 给 ~120）。单核调试用 `run-kernel-test`、-smp 调试用 `run-kernel-test-smp`（QEMU 真内核 ~954 项）；`cmake --build build --target run`（GUI 无断言）非验证。**⚠️ 根目录无 Makefile——所有 target 都走 `cmake --build build --target <name>`**（`run`/`run-smp`/`run-kernel-test`/`run-kernel-test-all`/`test_host` 等；定义见 `cmake/qemu.cmake` + `test/CMakeLists.txt`。**别写 `make run`/`make test_host`，根目录跑不了**）。**QEMU 易挂死（panic 死循环/死锁不返回）——验证与冒烟一律 `timeout` 包裹**：单 leg `timeout 40`（`run-kernel-test`/`run`），双 leg `timeout 120`（`run-kernel-test-all`）。**run-kernel-test 不编译 `test/unit/` host 单测**，改公共接口/InodeOps/mock 后 push 前补 `cmake --build build -j$(nproc)` 全量（CI 另跑 `cmake --build build --target test_host`，详见 DIRECTIVES L5）。
- 一批一 commit 一验证；绿才提交。
- 提交信息 `<type>(<scope>): <中文简述>`（纯描述变更；里程碑归属看 PLAN.md，不入 msg），**不带 Co-Authored-By / AI 署名**。
- 改前查牵连（grep 引用方），同步 ROADMAP↔PLAN↔document/todo↔git。
- 新里程碑/跨子系统大改：propose-then-execute。
- 写代码遵循 `document/ai/CODING-TASTE.md`：标识符+注释一律英文、私有成员 `_`、常量/枚举值 `k`前缀（目标）、提交前跑 clang-format。

## 命令（`.claude/commands/`）
战术：`/resume` `/status` `/next [批]` `/done`
战略：`/roadmap` `/milestone [M]` `/audit`

## pre-commit 钩子（`.githooks/pre-commit`）
跑 `scripts/check_line_limits.py --hpp 500`（与 CI 同款，拦超 500 行 .cpp/.S / 300 行 .hpp/.h）。**新 clone 一次性启用**：`git config core.hooksPath .githooks`（已为本仓库设好）。WIP 临时绕过：`git commit --no-verify`（CI 仍会拦）。

## 回到仓库
`/resume`（读 PLAN.md + git log）。Codex 等价粘贴 prompt 见 `document/ai/prompts.md`（`AGENTS.md` 自洽）。
