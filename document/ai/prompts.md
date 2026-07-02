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

## /preflight [目标]
读 document/ai/PLAN.md、DIRECTIVES.md、QUALITY-GATES.md。针对「目标」做改前预审:①范围/非范围 ②触及文件与调用方(`rg`) ③风险等级(R0-R5) ④命中的风险域 ⑤必须守住的不变量 ⑥验证矩阵 ⑦需同步文档。若属新里程碑/跨子系统大改,停下等确认;否则给可执行批计划。

## /quality-review
跑 git status --short、git --no-pager diff --stat、git --no-pager diff。读 QUALITY-GATES.md,按 G0-G8 输出 pass/fail/n/a;高危 findings 先列 file:line;给最小补救建议。只报告,除非明确要求修。

## /infra-audit [维度]
读 document/todo/quality/audit-guide.md 与 document/todo/quality/debt.md。按「维度,留空=下一待审维度」执行深度审计:①列搜索式 ②读真实代码取证 ③写 reports/<date>-<dimensions>-audit.md ④登记候选债务(含位置/根因/触发/修复/验证) ⑤更新 debt.md 与 quality/README.md 审计进度。默认只登记不修复。

## /fix-debt [DEBT-NNN]
读 document/todo/quality/debt.md、document/ai/QUALITY-GATES.md、document/todo/quality/audit-guide.md。对指定债务 propose 修复批:①根因复核 ②触及文件/调用方 ③设计与同步策略 ④测试计划 ⑤文档同步 ⑥commit 草案。停下等确认;确认后按一债一批执行,绿后更新 debt.md+notes。
