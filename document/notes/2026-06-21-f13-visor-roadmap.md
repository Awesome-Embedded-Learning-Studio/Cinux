# F13 visor 跨平台 GUI 库 — 开发清单与起步(2026-06-21,DRAFT v2)

> ⚠️ **DRAFT / 草稿**。来源同 [research](2026-06-21-f13-visor-research.md) / [architecture](2026-06-21-f13-visor-architecture.md) 笔记。未实现验证,方向可能调整。
>
> **v2 修订(2026-06-21)**:吸收外部审查 `review.md`——**PIT 反转提前到 M1**(调度模型先于绘制抽象);GPU M7 改 **texture compositor**(非 primitive CommandBuffer);加 **测试基建**(golden/replay/fuzz);文本 M5 只 ASCII/PSF;profile 3 个(合并 MCU-LCD);spawn 前置✅。

## §1 visor 开发清单(M0-M9,v2 重排:PIT 反转提前)

| M | 目标 | 关键产出 | 验收 |
|---|------|---------|------|
| **M0** | 仓库骨架 + Host ABI + profile + spawn 前置 | visor 仓库(双构建 host gcc + arm-none-eabi-gcc);`third_party/visor` submodule;`visor_host.h` **核心表 + Desktop extension**(v2);`visor_conf.h` **3 profile**;`visor_event` 定宽头;SDL 画色块;CI nm 门禁 + **golden/pixel CRC**(v2)。**spawn 前置✅**(commit `82e9023`) | 双构建过;SDL 画色块;MCU-F1 nm 零浮点/RTTI;spawn✅ run-kernel-test 887/0 |
| **M1** | **PIT-IRQ-composite 反转 + pump**(v2 提前) | `gui_pump`/`visor_pump(now_ms)` 状态机 + wake_cb;**worker 线程接管 dequeue+composite,退役 PIT IRQ0 composite**;用现有 Canvas/WM(不依赖 visor ABI 空壳) | run-kernel-test 绿 + 冒烟鼠键一致;**硬断言:PIT 完全不送达 GUI 仍刷新**(顺修 APIC 1-tick 债);pump 跑内核线程非 IRQ0 |
| **M2** | 核心绘制引擎 L4a(原 M1) | SwRaster(fill/blit/blend 定点/glyph/line)+ **Region 一等 + 退化**(v2)+ **dirty lowering**(rect/page-band/tile)+ **像素格式硬契约**(v2)+ PSF 子集化;废弃 colorkey 改真 alpha | SDL 画矩形/线/文本 + 脏区重绘(非全屏);STM32F1 编译零浮点;alpha 定点单测;**region fuzz**(v2) |
| **M3** | 显示后端三档 + 电源 | FULL/PARTIAL/PAGE + 电源状态机;**flush 模型**(v2,非 begin_frame->pointer);CinuxCanvasBackend;PageBackend/PartialBackend 示例 | Cinux QEMU 与重构前一致;**STM32F103C8+SSD1306 真板 RAM<20KB 实测** |
| **M4** | 输入后端 + pump 完善 | InputBackend(poll_event)+ visor 内部 SPSC 队列;**input capture/focus/grab**(v2,审查 S2.1) | QEMU 鼠键 + 拖窗/弹窗 pointer capture;MCU super-loop+RTOS 跑同一份 pump |
| **M5** | 窗口合成 + Widget 解耦 | Surface 四态(进程内,**不早期 Wayland 化** v2)+ dirty 合成 + Decorator;Window(Surface) vs Widget(子树);Label/Button/Slider;**MCU 独立 micro renderer**(v2);**文本只 ASCII/PSF**(v2,shaping 留 M9) | 多窗口拖动/焦点;Terminal 作根 Widget;未启用控件 gc 从 MCU 消除 |
| **M6** | 主题 + 能力降级 + 字体子集 | 主题(桌面圆角阴影/MCU 纯色降级)+ PSF2/TTF 裁到 xn/xr/CJK block | 同控件 Desktop/MCU 双 profile 正确;字体裁剪 Flash 降 |
| **M7** | **GPU texture compositor**(v2,非 primitive) | GPU 只合成 surface(scale/alpha/damage),widget 仍软件画;`can_render` 早期只 **All/None**(Partial 需 opt-in + 统计);KMS 形状 syscall 后端 | SwRaster↔compositor 切换核心零改动;命中率统计;**不在 M7 定 primitive draw-list** |
| **M8** | 进程内→多进程升级 | Surface 协议多进程(attach/damage/commit/release 经 IPC)+ Cinux 共享内存/evdev syscall | 两进程 client/server 行为同进程内;kill client 不崩 |
| **M9** | 桌面子系统 L4b 长弧 | Text 排版(shaping/bidi,此时才引)/矢量/动画/HiDPI/a11y(Desktop-only,分批) | 富文本/圆角阴影/动效/屏幕阅读器;L4b 在 MCU-F1 nm 零符号 |

## §2 Cinux 侧 F13 工作(留 CinuxOS 仓库)

F13 在 CinuxOS 侧收窄为 **L7 host adapter + 前置重构**,visor 主体在独立仓库:

1. **spawn 公共化前置** ✅(commit `82e9023`):`launch_user_program` 提取,init/gui_init 两处收敛。887/0 + 冒烟。
2. **PIT-IRQ-composite 反转**(v2 提前到 M1,紧跟 spawn):worker 线程 pump 接管,PIT ISR 退役或仅 enqueue;worker 优先级拉高。**最危险的可靠性债,先于绘制抽象**。
3. **Cinux host adapter**(M0/M3):填核心表(flush 转发 Canvas+flip / poll_event 转发 Mouse / now_ms / alloc=kmalloc)+ Desktop extension(spawn 转发 launch_user_program)。

## §3 「必须先做」(v2:PIT 反转紧随 spawn)

1. **spawn 公共化** ✅ 已完成。
2. **PIT-IRQ-composite 反转**(v2 第二步,提前)——IRQ 跑渲染是雷区,顺修「APIC 只送 1 tick」可靠性债。**先于绘制抽象**(调度模型先正确)。
3. **STM32F1 推迟到 M3 真板验证后**——canvas 1.2MiB / event 3KiB / terminal 23.4KiB 是 F1 RAM 死罪,只有 PageBackend + micro renderer + 对象池到位并真板 RAM<20KB 实测后,F1 才不是架构撕裂。

## §4 诚实预期(对第一次做 GUI 的用户)

- **visor 短期承诺**:比现在好看的 Cinux 桌面 + MCU 仪表盘。
- **完整 macOS/Windows 级桌面**:Desktop-only 的 L4b 长弧,非 M0-M5 可交付。
- **STM32F1 与桌面控件库不同 ceiling**(v2 强化):统一的是底座 + 协议 + 资源格式,**不是同一 widget engine**。MCU 走独立 micro renderer。
- **GPU 演进(v2)**:先 texture compositor(合成 surface),primitive draw-list 推迟到有真 GPU。不在早期定 CommandBuffer。
- **多进程同构(v2 收紧)**:Day-1 只保留 Surface ownership 不变量,不早期 Wayland 化。

## §5 起步建议(v2:spawn✓ → PIT 反转 → visor 仓库)

1. **spawn 公共化** ✅ 已完成。
2. **下一步:PIT 反转**(M1,Cinux 仓库内,不依赖 visor 仓库)。worker pump 接管,行为不变去 IRQ composite。最危险可靠性债,先解决。
3. **建 visor 仓库**(`third_party/visor` submodule):Host ABI 核心表+extension 骨架 + SDL 画色块 + 3 profile + CI 双构建 + golden 测试。
4. **M2 绘制引擎 + M3 显示后端**,然后 **M3 才引入 STM32F1 真板**。

## §6 开放问题(实现时定)

- visor 仓库 license / 命名空间(`visor::` vs `cinux::gui::`?)。
- visor core 是否复用 Cinux-Base 的 `ErrorOr/StringView/Span`(子模块依赖方向)。
- texture compositor 接口(M7 有真 GPU 后定)。
- Cinux host adapter 的 visor_pump 跑哪个内核线程(现 gui_worker_thread 升级)。
- STM32F1 真板/cortex-m QEMU 验证基建何时搭(M3 前置)。

> **DRAFT v2 备注**:M 编号 v2 重排(PIT 反转 M1 提前);M9 可能按子系统再拆 M9a/M9b...。起步建议(§5)是当前推荐。
