# F13 visor 跨平台 GUI 库 — 开发清单与起步(2026-06-21)

> ⚠️ **DRAFT / 草稿**。来源同 [research](2026-06-21-f13-visor-research.md) / [architecture](2026-06-21-f13-visor-architecture.md) 笔记。未实现验证,方向可能调整。
>
> 本文给 visor 的 **M0-M9 开发清单**(放 visor 仓库)+ **Cinux 侧 F13 工作** + **诚实预期与起步建议**。用户第一次做 GUI,起步路径是重点。

## §1 visor 开发清单(M0-M9,放 visor 仓库)

| M | 目标 | 关键产出 | 验收 |
|---|------|---------|------|
| **M0** | 仓库骨架 + Host ABI 表 + profile + spawn 前置 | visor 仓库(CMake 双构建 host gcc + arm-none-eabi-gcc);`third_party/visor` submodule;`visor_host.h` 5 张表;`visor_conf.h` 4 profile;`visor_event` 定宽头;SDL 画色块;CI nm 门禁。**前置(Cinux 仓库,独立 PR)**:合并 init.cpp / gui_init.cpp shell 启动 | 双构建过;SDL 画色块;MCU-F1 nm 零浮点/RTTI;合并后 run-kernel-test 绿 + make run 冒烟 shell 行为不变 |
| **M1** | 核心绘制引擎 L4a | PrimitiveSink(fill/blit/blend 定点/glyph/line)+ SwRaster + 像素格式枚举 + 脏区最小版 + PSF2 子集化;废弃 colorkey 改真 alpha mask | SDL 画矩形/线/文本 + 脏区重绘(非全屏);STM32F1 编译零浮点;alpha 定点单测;STM32F1 blend fps 基准 |
| **M2** | 显示后端三档 + 电源 | FULL/PARTIAL/PAGE + 电源状态机;CinuxCanvasBackend;PageBackend/PartialBackend 示例 | Cinux QEMU 与重构前一致;**STM32F103C8+SSD1306 真板 RAM<20KB 实测**;电源 Stop 模式可关断 |
| **M3** | 输入后端 + pump 反转 | InputBackend + pump 状态机 + wake_cb;**反转 PIT-IRQ-composite 为 worker 线程 pump**;visor 内部 SPSC 队列 | QEMU 鼠键一致;**硬断言:PIT 完全不送达 GUI 仍刷新**(顺修 APIC 1-tick 债);MCU super-loop+RTOS 两种跑同一份 pump |
| **M4** | 窗口系统 + 合成器 | Surface 四态协议 + dirty-region 合成 + 事件路由 + Decorator | 多窗口拖动/关闭/焦点一致;flush 只报脏矩形;Decorator paint/hit_test 一致 |
| **M5** | Widget 解耦 + 控件骨架 | Window(Surface) vs Widget(子树)分离;Label/Button/Slider;flex 骨架;**a11y/UTF-8 hook Day-1 预留**;Desktop 虚函数/MCU 对象池 | Terminal 作根 Widget 正常;Button CLICKED;未启用控件 gc 从 MCU 消除(nm 0 符号,单控件增量<1KB) |
| **M6** | 主题 + 能力降级 + 字体子集 | 主题(桌面圆角阴影/MCU 纯色降级)+ PSF2/TTF 裁到 xn/xr/CJK block;CommandBuffer 高层(Desktop) | 同控件 Desktop/MCU 双 profile 正确;字体裁剪 Flash 降 |
| **M7** | GPU/2D 后端预留 | Gpu2DBackend(模拟 DMA2D)+ Dispatcher 整段分流 + `can_render(All/Partial/None)`+ KMS 形状 syscall 后端 | SwRaster↔Gpu2D 切换核心零改动;命中率统计 |
| **M8** | 进程内→多进程升级 | Surface 协议多进程实现 + Cinux 共享内存/evdev syscall | 两进程 client/server 行为同进程内;kill client 不崩。**「现在内核态/未来用户态」同构端到端证明** |
| **M9** | 桌面子系统 L4b 长弧 | Text 排版/矢量/动画/HiDPI/a11y(Desktop-only,分批) | 富文本/圆角阴影/动效/屏幕阅读器;L4b 在 MCU-F1 nm 零符号 |

## §2 Cinux 侧 F13 工作(留 CinuxOS 仓库)

F13 在 CinuxOS 侧收窄为 **L7 host adapter 的实现 + 三个前置重构**,visor 主体在独立仓库:

1. **spawn 公共化前置**(M0 前置,独立 PR):合并 [init.cpp:81-140](../../kernel/proc/init.cpp#L81-L140) 与 [gui_init.cpp:94-159](../../kernel/gui/gui_init.cpp#L94-L159) 的 shell 启动成单条 fork→execve 公共路径;处理 TaskBuilder 全局 tid 污染(GOTCHA#22);F2-M5 Stack VMA 硬门控三处合一。
2. **Cinux host adapter**:填 `visor_host` 5 张表(Display→Canvas+flip;Input→Mouse 队列+keyboard;Time→now_ms;pump→gui_worker_thread 循环;spawn→公共 fork→execve)。
3. **PIT-IRQ-composite 反转**(M3):worker 线程接管全部,PIT ISR 退役或仅 enqueue;worker 优先级在 F3 调度器侧拉高。

## §3 三个「必须先做」(对抗审查揪出,针对现有代码)

1. **spawn 合并前置**——否则 L7 adapter 是重写,且是迁移最易回归点。
2. **PIT-IRQ-composite 反转**——IRQ 跑渲染是雷区,顺修「APIC 只送 1 tick」可靠性债。
3. **STM32F1 推迟到 M2 真板验证后**——canvas 1.2MiB / event 3KiB / terminal 23.4KiB 三块是 F1 RAM 死罪,只有 PageBackend + 绘制引擎 + 对象池全部到位并真板 RAM<20KB 实测后,F1 才不是架构撕裂。此前 F1 只是 profile 预设里的编译目标,不做交付承诺。

## §4 诚实预期(对第一次做 GUI 的用户)

- **visor 短期承诺**:比现在好看的 Cinux 桌面 + MCU 仪表盘。
- **完整 macOS/Windows 级桌面**:Desktop-profile-only 的 L4b(Text/矢量/动画/HiDPI/a11y)逐步长出来的**长弧**,非 M0-M5 可交付。不做就退化成 Win3.1 闪现。
- **STM32F1 与桌面控件库不同 ceiling**:别指望一份代码同等深度。MCU-F1 只到绘制引擎+Label/Button 子集。
- **「现在内核态/未来用户态 server 同构」是设计约束,不是早期果实**:M8 才端到端验证。早期把它当目标设计(Day-1 Surface 四态协议 + 合成器独占真理源),不当早期可交付。

## §5 起步建议(三步走,最稳)

1. **先别动 visor 新仓库**。在 CinuxOS 仓库做 M0 前置重构(spawn 合并),独立 PR + `timeout 40` QEMU 验证。这是 visor 能成立的前提。
2. **建 visor 仓库**(`third_party/visor` submodule),只交付 M0:Host ABI 表骨架 + SDL 画色块 + 4 profile + CI 双构建门禁。Cinux adapter 先只填 Display/Input 两张表,让 visor M1 绘制引擎在 Cinux QEMU 上画出和重构前一致的桌面——验证「同一份 core 跑 Cinux 内核态」最窄切片。
3. **M1 绘制引擎 + M3 pump 反转**,然后 **M2 才引入 STM32F1 真板**。

## §6 开放问题(实现时定)

- visor 仓库的 license / 命名空间(`visor::` vs `cinux::gui::`?)。
- visor core 是否复用 Cinux-Base 的 `ErrorOr/StringView/Span`(子模块依赖方向)。
- Desktop 子系统开关粒度(整组 vs 每子系统)。
- Cinux host adapter 的 visor_pump 跑在哪个内核线程(现 gui_worker_thread 升级 vs 新建)。
- STM32F1 真板/真 QEMU cortex-m 的验证基建何时搭(M2 前置)。

> **DRAFT 备注**:M 编号与范围是初版;M9(桌面子系统长弧)可能按子系统再拆成 M9a/M9b...。起步建议(§5)是当前推荐,用户确认后写进 PLAN.md。
