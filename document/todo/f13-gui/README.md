# F13: GUI 分离 → visor 跨平台 GUI 库（DRAFT）

> ⚠️ **DRAFT / 草稿（2026-06-21）**。方向可能调整。来源:11-agent workflow 调研 + 3-lens 对抗验证 + 用户 4 决策。

## 方向转变（取代 2026-05 旧草案）

visor 不是「Cinux 的 GUI」,而是一个**跨平台 GUI 库**:同一份 core 跑 Cinux 桌面（现内核态 / 未来用户态 server）、单片机 LCD/OLED（小到 STM32F103）、未来显卡。内核只提供最小能力,**完全不感知 GUI 风格/主题/控件**。

用户 4 决策:① 完整控件工具箱（Windows/macOS 级）② Cinux 桌面先 ③ MCU 全规模含 STM32F1 ④ GPU 可插拔先软件。

核心机制:**Host ABI 函数指针表（visor_host.h）是唯一硬边界**——换宿主（内核态/用户态/MCU/host）只换 5 张表的填充。这就是「甚至不感知是否用户态」。

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

第一步（不依赖 visor 仓库,现在就能做）:**spawn 公共化前置重构**（合并 [init.cpp](../../../kernel/proc/init.cpp) / [gui_init.cpp](../../../kernel/gui/gui_init.cpp) shell 启动）。详见 [visor-02 §1](visor-02-refactor-and-separation.md)。
