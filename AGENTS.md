# CinuxOS — Codex 工作指引

> Codex 自动读本文件；看不到 CLAUDE.md 与 Claude 私有 memory，故本文件自洽。等价命令的粘贴式 prompt 见 `document/ai/prompts.md`。

C++17 内核，13 Feature / ~60 Milestone。无异常（禁 throw/try/catch），错误经 `cinux::lib::ErrorOr<T>`（子模块 `third_party/Cinux-Base/include/cinux/expected.hpp`，勿在 kernel/ 重写）。

## 文档分层（按需读）
- `document/ai/DIRECTIVES.md` — 架构铁律 + 约定 + 操作模型
- `document/ai/ROADMAP.md` — 里程碑全树 + 状态
- `document/ai/PLAN.md` — 当前焦点批级进度

## 始终遵守
- 验证只用 `cmake --build build --target run-kernel-test -j$(nproc)`（~662 项，本机 14 核并行）；`run`/`test_host`/`make run` 非验证。
- 一批一 commit 一验证；绿才提交。
- 提交信息 `<type>(<scope>): <中文简述>`（纯描述变更；里程碑归属看 PLAN.md，不入 msg），**不带 Co-Authored-By / AI 署名**。
- 改前查牵连（grep 引用方），同步 ROADMAP↔PLAN↔document/todo↔git。
- Cinux-Base：header-only/constexpr/无堆/禁 RTTI/C++17/`cinux::lib`/单文件 ≤ 400 行。
- 新里程碑/跨子系统大改：propose-then-execute。

## 回到仓库
读 `document/ai/PLAN.md` + `git log --oneline -15`，再决定下一步。粘贴式 prompt 见 `document/ai/prompts.md`。详见 `document/ai/DIRECTIVES.md`。
