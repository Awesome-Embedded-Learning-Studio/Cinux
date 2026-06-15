# CinuxOS 开发日志（DEVLOG）

> 编年工作日志：**每批 / 每个有意义的迭代一条**（`/done` 自动追加，最新在最上）。
> 像介绍自己这轮工作那样写——**粗略改了什么 + 为什么/决策 + 陷阱/弯路 + 验证结果**。
> 分工：git=机械 diff；PLAN.md=当前状态；OPEN GOTCHAS=活警告；**本文=编年叙事+决策**。条目写定不改（要更正就新写一条）。

**条目模板**（`/done` 按此填，粗略即可、非 diff 复述）：
```
## YYYY-MM-DD <批# / 里程碑 / meta> — <一句话标题> (commit <short>)
- 改动：<哪些文件/区域做了什么，2-4 行>
- 决策/why：<关键取舍、为什么这么做>
- 陷阱/弯路：<gotchas、试错、差点漏的；无则写"无">
- 验证：<测试结果/状态>
```

<!-- 新条目插在此行下方（最新在最上） -->

## 2026-06-15 meta — CODING-TASTE 风格权威 + 接线 (commit 8831b24)
- 改动：新增 `document/ai/CODING-TASTE.md`（扎根 `.clang-format`+真实代码）；`DIRECTIVES` B、`CLAUDE.md`、`AGENTS.md` 加指针；旧 `document/ai_prompts/code_conventions.md` 与本地 `helpers/CodingStyle.md` 转指针。
- 决策/why：注释**全英文**（代码现状如此）；常量/枚举值**迁 k 前缀**（目标态，legacy 迁移中、不批量重写）；clang-format 为机械风格唯一权威。
- 陷阱/弯路：旧 `code_conventions.md`（Milestone-0）与代码脱节约 10 处（C++20、namespace 缩进、指针左右、中文示例…）——正是"乱"的根因，故取代而非沿用。
- 验证：纯文档提交，未跑内核测试。

## 2026-06-15 批2b — read/write/readdir → ErrorOr<int64_t> (commit 6d47c99)
- 改动：ext2_common.cpp / ramdisk.cpp 的 Ext2(Ramdisk)FileOps/DirOps 的 read/write/readdir 改返回 `ErrorOr<int64_t>`；7 个调用方跟进（execve.cpp×3 read、sys_read、sys_write、sys_getdents、sys_rmdir）。
- 决策/why：`value==0` 作 OK（目录读完），`!ok` 作错误——压掉裸 int 三态歧义（正数/0/-1）。
- 陷阱/弯路：侦察只 grep 了箭头形态 `->ops->op(...)`，漏了点号形态 `ops_obj.op(...)`（test_pipe.cpp 局部对象），靠编译暴露；`PipeReadOps`/`PipeWriteOps` 也是 InodeOps 子类差点漏改，适配层须把底层 `-1` 显式翻译成 `Error`。
- 验证：662 passed, 0 failed。

## 2026-06-15 meta — 分层架构 + 7 slash 命令 + Codex 对等 (commit b95f022)
- 改动：新增 `document/ai/`（DIRECTIVES/ROADMAP/PLAN/prompts）+ 仓库根 `CLAUDE.md`/`AGENTS.md` 薄指针 + `.claude/commands/` 7 命令（战术 resume/status/next/done + 战略 roadmap/milestone/audit）；`.gitignore` 放行 `.claude/commands/` 与 `CLAUDE.md`。
- 决策/why：耐久度三层（DIRECTIVES 年 / ROADMAP 里程碑 / PLAN 批）；事实源进仓库让 Claude 与 Codex 共享；commit msg 纯描述、**无 Co-Authored-By**；验证命令固化 `-j$(nproc)`。
- 陷阱/弯路：初版定位"handoff 战术命令"被否——重定向为"Claude 长期主力开发"导向；`.claude/`/`CLAUDE.md` 原被 gitignore，发现后改规则放行（settings.local.json 仍忽略）。
- 验证：命令 bash 片段实测（git log / grep readdir）；纯文档提交。

## 2026-06-15 批1+批2a — VFS/InodeOps 引入 ErrorOr (commit 93e2870)
- 改动：`FileSystem::mount/lookup` → `ErrorOr<void>`/`ErrorOr<Inode*>`；`InodeOps::create/mkdir/unlink/stat` → `ErrorOr`；ramdisk/ext2 实现 + init/execve + ~14 syscall 消费方跟进。（事后补记，细节见 PLAN.md）
- 决策/why：先改 VFS 接口与写/元数据 ops；read/write/readdir 因三态歧义留批2b 单独处理。
- 陷阱/弯路：测试 helper（big_kernel_test.h 的 lookup_or_null 等）把 ErrorOr 降级回 nullptr/0-1 以适配旧断言；`__assert_fail` 须在 crt_stub.cpp 提供以支撑 `<cassert>`。
- 验证：662 passed, 0 failed（两次）。
