> ⚠️ **SUPERSEDED (2026-07-05)**:本目录(visor 七层草案)已被 **Cinux-GUI 独立仓**([Awesome-Embedded-Learning-Studio/Cinux-GUI](https://github.com/Awesome-Embedded-Learning-Studio/Cinux-GUI))的 P0-P7 实现超越。`visor` 名已废(→ Cinux-GUI),M0-M9 编号 → P0-P9。**当前权威**:Cinux-GUI 仓 `document/ai/{ROADMAP,PLAN,DIRECTIVES}.md`。F13 在 Cinux 侧收窄为 host adapter 重写,见 Cinux `document/ai/PLAN.md` 🔄 F13-B 段。本目录保留作历史背景(visor 设计演进 + 外部审查 `review-external.md`)。

# F13: GUI 分离 → visor 跨平台 GUI 库（DRAFT）

> ⚠️ **DRAFT v2 / 草稿（2026-06-21）**。方向可能调整。来源:11-agent workflow 调研 + 3-lens 对抗验证 + 用户 4 决策 + **外部 AI 审查(`review.md`)v2 采纳**。

## 方向转变（取代 2026-05 旧草案）

visor 不是「Cinux 的 GUI」,而是一个**跨平台 GUI 库**:同一份 core 跑 Cinux 桌面（现内核态 / 未来用户态 server）、单片机 LCD/OLED（小到 STM32F103）、未来显卡。内核只提供最小能力,**完全不感知 GUI 风格/主题/控件**。

用户 4 决策:① 完整控件工具箱（Windows/macOS 级）② Cinux 桌面先 ③ MCU 全规模含 STM32F1 ④ GPU 可插拔先软件。

核心机制:**Host ABI 函数指针表（visor_host.h）是唯一硬边界**——**核心表**(Display/Input/Time/Memory)+ **Desktop extension**(spawn/rpc,MCU 永远 NULL),换宿主只换表填充。这就是「甚至不感知是否用户态」。**v2(审查)关键调整**:Display 改 `flush` 模型(非 begin_frame→pointer)、GPU 改 texture compositor 优先、**PIT-IRQ-composite 反转提前**到绘制抽象前。

诚实预期:STM32F1 与桌面控件库是两个 profile ceiling,visor 短期承诺「比现在好看的 Cinux 桌面 + MCU 仪表盘」,完整 macOS/Windows 级是 L4b 长弧。

## 文档清单（新方案）

| 文档 | 内容 |
|------|------|
| [visor-01-presets.md](visor-01-presets.md) | **预设**:4 profile + VISOR_* 宏 + core 约束 + ABI 契约骨架 |
| [visor-02-refactor-and-separation.md](visor-02-refactor-and-separation.md) | 重构与代码分离执行计划 |
| [../../notes/2026-06-21-f13-visor-research.md](../../notes/2026-06-21-f13-visor-research.md) | 调研结论（先例借鉴 + STM32F1 可行性 + 对抗裂缝） |
| [../../notes/2026-06-21-f13-visor-architecture.md](../../notes/2026-06-21-f13-visor-architecture.md) | 七层架构 + Host ABI 边界 + 现状映射 |
| [../../notes/2026-06-21-f13-visor-roadmap.md](../../notes/2026-06-21-f13-visor-roadmap.md) | M0-M9 开发清单 + 起步 |

## 旧草案（已 superseded,保留历史）

- [00-gui-abi.md](00-gui-abi.md) — 旧 gui_abi.hpp C ABI 方向（被 visor-01 presets 取代）
- [02-gui-adapter.md](02-gui-adapter.md) — 旧 thin adapter（被 visor-02 refactor 取代）
- [03-gui-decouple.md](03-gui-decouple.md) — 旧解耦（被新七层架构取代）

## 起步

**spawn 公共化前置** ✅ 已完成(commit `82e9023`)。**下一步(不依赖 visor 仓库):PIT-IRQ-composite 反转**(worker 线程 pump 接管,行为不变去 IRQ composite——最危险可靠性债,先于绘制抽象)。详见 [visor-02 §2](visor-02-refactor-and-separation.md)。
