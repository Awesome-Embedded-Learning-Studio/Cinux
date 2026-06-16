---
description: 验证当前批 — run-kernel-test 绿则更新 PLAN 并起草提交(无 Co-Auth)，红则只报
allowed-tools: Bash(cmake:*), Bash(git:*), Edit
---
@document/ai/DIRECTIVES.md
跑唯一验证：!`cmake --build build --target run-kernel-test -j$(nproc)`
判定：末尾 passed 且 0 failed = 绿。
- 红：列失败项，不改 PLAN、不提交，给方向后停。
- 绿：① 更新 PLAN.md(批标✅+commit短hash+测试数；挪 NEXT；里程碑完成则同步 ROADMAP 状态) ② 起草提交 `<type>(<scope>): <简述>`（纯描述，里程碑归属看 PLAN.md），**不带 Co-Authored-By/AI 署名**，不自动提交 ③ **写 `document/notes/<date>-<topic>.md`**（正式发布质量：背景/目标/设计/决策/陷阱/验证，参考 `document/notes/` 既有风格；非 diff 复述。里程碑可多篇或一篇总结） ④ 报告改动+测试+建议 git 命令。
禁止把 run/test_host 的通过当门。
