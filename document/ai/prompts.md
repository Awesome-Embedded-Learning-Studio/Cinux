# Codex 等价命令（粘贴式 prompt）

> Codex 无 slash 命令；复制下列整段进对话即可。Codex 会自动读 AGENTS.md。

## /resume
读 document/ai/PLAN.md 与 document/ai/DIRECTIVES.md，跑 git log --oneline -10。三行回答：①你在哪(最近✅批+commit) ②下一步(🔄批+范围,grep 定位文件) ③相关陷阱。git 与 PLAN 不一致先指出。只读不改。

## /status
读 document/ai/PLAN.md，紧凑打印批表(状态/范围/commit/测试)+最近一条 git log --oneline -1。只读。

## /next [批-id]
读 PLAN.md 与 DIRECTIVES.md。为「批<id,留空=🔄NEXT>」产出脚手架：①范围 ②触及文件(grep 绝对路径;批不存在报错停) ③ErrorOr 签名草案 ④完成门 run-kernel-test 全绿 ⑤提交草案(无 Co-Auth) ⑥gotcha。停下等确认,不开改。

## /done
跑 cmake --build build --target run-kernel-test -j$(nproc)(别用 run/test_host 当门)。passed 且 0 failed=绿。
- 红:列失败项,不改 PLAN,不提交,给方向后停。
- 绿:①更新 PLAN.md(批标✅+commit短hash+测试数,挪 NEXT,必要时同步 ROADMAP) ②起草提交 `<type>(<scope>): <简述>` 无 Co-Auth,不自动提交 ③写 document/notes/<date>-<topic>.md(正式发布质量:背景/目标/设计/决策/陷阱/验证,非 diff 复述) ④报告改动+测试+建议 git 命令。

## /roadmap
读 document/ai/ROADMAP.md,紧凑打印里程碑树+依赖瓶颈,指出"当前焦点之后下一个可启动的里程碑/Feature"。只读。

## /milestone [M-id]
读 ROADMAP.md 与 DIRECTIVES.md。为「<id,默认下一未启动里程碑>」propose:①目标范围 ②批分解(每批≈一commit+完成门) ③触及子系统/文件(grep) ④与架构不变量契合点 ⑤风险/依赖。草案停下等确认;确认后写入 PLAN 并在 ROADMAP 标🔄。不开改。

## /audit
跑 git --no-pager diff --stat 与 git --no-pager diff。对照 DIRECTIVES 逐条查:命名/注释/无异常/ErrorOr用法/子模块边界/syscall翻译边界/提交格式(无Co-Auth)。列违规+定位+建议。只报告(除非要求改)。
