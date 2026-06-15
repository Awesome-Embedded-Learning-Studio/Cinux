---
description: 为下一里程碑拆批 — propose 草案等确认（roadmap→plan 桥）
argument-hint: "[里程碑-id，默认 ROADMAP 下一未启动]"
allowed-tools: Bash(grep:*), Bash(ls:*), Read, Grep, Glob
---
@document/ai/ROADMAP.md
@document/ai/DIRECTIVES.md
为「$1」（默认下一未启动里程碑）propose：①目标与范围 ②批分解(每批≈一commit+完成门) ③触及子系统/文件(grep 定位) ④与 DIRECTIVES 架构不变量的契合点 ⑤风险/依赖。
草案停下等确认；确认后再写入 PLAN.md 并在 ROADMAP 标该里程碑 🔄。不直接开改。
