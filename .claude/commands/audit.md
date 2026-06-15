---
description: 当前 diff 对照 DIRECTIVES 自检（约定+架构不变量）
allowed-tools: Bash(git diff:*), Bash(git status:*), Bash(git --no-pager diff:*), Read, Grep
---
@document/ai/DIRECTIVES.md
当前改动：!`git --no-pager diff --stat`
!`git --no-pager diff`
对照 DIRECTIVES 逐条检查：命名/注释/无异常/ErrorOr 用法/子模块边界/syscall 翻译边界/提交格式(无 Co-Auth)。列违规+定位+建议修法。只报告，不自动改（除非要求）。
