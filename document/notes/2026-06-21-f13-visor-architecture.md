# F13 visor 跨平台 GUI 库 — 七层架构与 ABI 边界(2026-06-21)

> ⚠️ **DRAFT / 草稿**。来源同 [research 笔记](2026-06-21-f13-visor-research.md)(11-agent workflow + 对抗验证)。未实现验证,方向可能调整。
>
> 本文给 visor 的**分层架构 + Host ABI 边界 + 设计原则**,并映射 CinuxOS 现有代码落到哪层。配套:[presets](../todo/f13-gui/visor-01-presets.md)(profile/宏/约束)、[refactor 计划](../todo/f13-gui/visor-02-refactor-and-separation.md)。

## §1 七层总览

```
┌─────────────────────────────────────────────────────────────┐
│  应用(Cinux 桌面 shell / MCU 仪表盘 / 未来 GUI app)           │ ← 你写的 UI
├─────────────────────────────────────────────────────────────┤
│ L6  控件工具箱 Widget (Button/Label/Slider/...)   Desktop ◀ VISOR_USE_* 编译期裁剪
├─────────────────────────────────────────────────────────────┤
│ L5  窗口系统 + 合成器 (Surface 四态协议 + dirty-region)         │ ◀ 进程内/多进程同构
├─────────────────────────────────────────────────────────────┤
│ L4b 桌面子系统 (Text排版/矢量/动画/HiDPI/a11y) Desktop-only    │
│ L4a 核心引擎 (脏区管理/pump/分配器/DPI)  ◀ MCU 可跑              │
├═════════════════════════════════════════════════════════════┤ ← 这条线以上 = visor 库(平台无关)
│ L3  渲染后端 RenderBackend (软件光栅 ←→ GPU/2D/3D 可插拔)        │
│ L2  输入后端 + 时间后端 (poll 事件 / now_ms)                   │
│ L1  显示后端 DisplayBackend (FULL/PARTIAL/STREAM-PAGE 三档)    │
├─────────────────────────────────────────────────────────────┤
│ L0  Host ABI 表 visor_host.h (5 张函数指针表,唯一硬边界)        │ ◀ visor 与宿主接缝
├─────────────────────────────────────────────────────────────┤
│ 宿主:Cinux adapter │ MCU adapter │ 未来 user server │ SDL sim  │ ◀ 各自填 5 张表
└─────────────────────────────────────────────────────────────┘
```

**「不感知是否用户态」的实现机制 = L0 Host ABI 表**:visor core 永远只对这 5 张函数指针表说话,换宿主(内核态/用户态/MCU/host)只换表的填充。

## §2 逐层职责与插拔边界

| 层 | 职责 | 插拔边界(换宿主只换这里) |
|---|---|---|
| **L0** Host ABI 表 | `visor_host.h`,5 张函数指针表(Display/Input/Time/Process/Rpc),freestanding、定宽事件头 | **唯一硬边界**。Cinux 桌面=直接函数转发(C++ 填表);MCU=裸机/RTOS 填表;未来 user server=syscall+mmap 填表;host SDL=libc 填表 |
| **L1** 显示后端 | 把矩形像素送屏。三档 FULL(整帧双缓冲,桌面)/ PARTIAL(1/N 屏,MCU 彩屏)/ STREAM-PAGE(逐 8 行,STM32F1 单色)。契约 `begin_frame(area)→指针 / commit / flush_ready` + 电源状态机 | Cinux=Canvas+flip MMIO;MCU 单色=PageBackend(u8g2 picture-loop,1KB);彩屏=SPI DMA;未来=KMS 形状 syscall。**抽象掉 canvas 的 1.2MiB 全帧 buffer** |
| **L2** 输入+时间后端 | `poll_event()` pull(Pointer/Keycode/Encoder/Touch 四类一等);`now_ms()` 注入时钟 + `next_deadline_ms()`(MCU WFI) | Cinux=转发 Mouse 队列+keyboard;MCU=GPIO/ADC/矩阵;未来=read(evdev)。**ISR 只 push,visor 永远非中断 dequeue** |
| **L3** 渲染后端 | PrimitiveSink(fill/blit/blit_blend 定点/glyph/line)+ 可选 CommandBuffer(GPU)。Capabilities 能力位 + `can_render(All/Partial/None)` 整段判定 | MVP=SwRaster(STM32F1 可跑);Gpu2D/GpuTexture 未来替换。**接口现在预留,先只交付 SW** |
| **L4a** 核心引擎 | 脏区管理(invalidate→只刷脏块)+ `visor_pump(now_ms)` + 分配器 + DPI 缩放 + Surface 四态协议 | 平台无关,三宿主同一份。**单 GUI 线程无锁**,跨核数据只经 SPSC 队列进 |
| **L4b** 桌面子系统 | Text 排版(TTF+HarfBuzz)/矢量/动画/HiDPI/a11y | **Desktop-profile-only,MCU nm 零符号**。把「桌面控件库」与「STM32F1」拆开的地方 |
| **L5** 窗口系统/合成器 | Surface 四态(attach/damage/commit/release)+ dirty-region 合成 + 事件路由 + Decorator | 进程内:widget 直画进 Surface;**多进程:同接口,提交序列化经 IPC,server 合成——零改动**。废弃全屏 composite |
| **L6** 控件工具箱 | Widget 树+布局+事件+样式+主题(能力降级) | `VISOR_USE_*` 编译期裁剪。Desktop=虚函数;MCU=CRTP/对象池 |
| **L7** Cinux host adapter | **留 CinuxOS 仓库**,填 5 张表的 C++ 实现 | gui_init.cpp 的 spawn 内联代码下沉到这。**误把 kernel 概念漏进 core 就破坏特权中立** |

## §3 设计原则(四条铁律)

1. **特权中立 = pump 模型**。`visor_pump(now_ms)` 主动 dequeue+composite,**绝不在 IRQ 里跑用户函数**。caller(Cinux 内核线程 / MCU main loop / 用户态 epoll)决定何时调。core 永不自己 busy-wait/epoll/yield/注册 IRQ。
2. **static-only core**。零 `new`/零 STL/`-fno-exceptions -fno-rtti -ffreestanding`/`VISOR_NO_FPU` 整数定点。分配走 `visor_set_allocator` 三指针。CI `nm` 断言零 `__aeabi_*`/`_ZTI`/`__cxa_throw`。
3. **dirty-region only**。只刷脏块——STM32F1 能否跑的分水岭。废弃全屏 composite。
4. **profile ceiling**。同一 core,但桌面级能力是 Desktop-only L4b;STM32F1 只承诺绘制引擎+Label/Button 子集。诚实承诺分层(见 presets §0)。

## §4 内核/用户态最小能力 ABI 边界(Cinux 这侧 = F13 实质工作)

7 条(详见 presets §4 ABI 骨架):

| 能力 | Cinux 现状 | 特权中立 ABI |
|---|---|---|
| 显示访问 | canvas 双缓冲 + flip MMIO | `begin_frame/commit/flush_ready` + Surface 四态 |
| 输入事件 | Mouse IRQ12→EventQueue,PIT IRQ0 里 dequeue(反模式) | `poll_event`,定宽头+可变尾,ISR 只 push |
| 定时 | PIT tick_callback(承认「只送 1 tick」) | `now_ms` 注入,**不绑死 PIT** |
| pump 驱动 | IRQ 里 composite + worker 只 spawn(两条并存) | `visor_pump→enum{Done,NeedMoreInput,NeedFlushComplete}` + `wake_cb` |
| 进程 spawn | gui_init.cpp:94-159 内联全套 | `spawn(path,argv,stdin,stdout)→pid`,**先合并 init.cpp 重复路径** |
| IPC/共享内存 | AddressSpace->map 可用,无 KMS/GPU | `RpcBackend`(初期 NULL),对齐 KMS 数据形状不链 libdrm |
| 像素格式+分配器 | 硬编码 uint32 + new[] | 像素格式枚举 + `visor_set_allocator`,core 零 new |

## §5 CinuxOS 现有代码 → visor 层映射(重构落点)

| 现有文件 | 当前职责 | → visor 层 | 重构动作 |
|---------|---------|-----------|---------|
| [gui/gui_init.cpp](../../kernel/gui/gui_init.cpp) | GUI 初始化 + shell spawn + PIT tick composite | **拆三处**:spawn→L7 adapter;tick composite→L4a pump(反转);WM init→L5 | spawn 合并公共路径(M0 前置);PIT 反转(M3) |
| [gui/window_manager.cpp](../../kernel/gui/window_manager.cpp) | 窗口管理 + 全屏 composite | L5 合成器 | 全屏 composite→dirty-region(M1/M4) |
| [gui/window.cpp](../../kernel/gui/window.cpp) | 顶层窗口 | L5 Surface + L6 Window | Window(Surface) vs Widget 解耦(M5) |
| [gui/terminal.cpp](../../kernel/gui/terminal.cpp) | 终端(继承 Window) | L6 Widget(Desktop only) | Terminal→Window 内根 Widget;加 `VISOR_USE_TERMINAL`(M5) |
| [gui/event.hpp](../../kernel/gui/event.hpp) | SPSC 事件队列 | L2(visor 内部队列)+ visor_event | 定宽头+可变尾;`VISOR_EVENT_QUEUE_SIZE`(M3) |
| [gui/desktop_icon.hpp](../../kernel/gui/desktop_icon.hpp) | 桌面图标 | L6(Launcher 控件) | 随控件库 |
| [drivers/canvas.hpp](../../kernel/drivers/canvas.hpp) | 双缓冲绘制 | **拆**:绘制原语→L3 SwRaster;back/front buffer→L1 DisplayBackend | 1.2MiB buffer→FULL 档(Desktop);colorkey→真 alpha(M1) |
| [drivers/video/framebuffer.hpp](../../kernel/drivers/video/framebuffer.hpp) | VBE fb | L1(CinuxCanvasBackend 后端) | adapter 填表 |
| [drivers/video/font.hpp](../../kernel/drivers/video/font.hpp) | PSF2 字体 | L3 glyph + L4b Text(Desktop) | 点阵=MCU;子集化(M1/M6) |
| [drivers/mouse.hpp](../../kernel/drivers/mouse.hpp) | PS/2 鼠标 + EventQueue | L2 InputBackend | ISR→visor_event push(M3) |
| `kernel/drivers/keyboard/` | 键盘扫描码 | L2 InputBackend | 同上 |

## §6 进程内 → 多进程同构性(Day-1 不变量)

visor 从第一天就按「未来能平滑升级到用户态多进程 server」设计,而非后期才补:

- **Surface 四态协议(attach/damage/commit/release)**:进程内 commit 后 swap double-buffer(所有权隐式);多进程 commit 后所有权移交 server、release 经 IPC 回传。**core 同构**,迁移点收敛到 host adapter 两实现。
- **z-order/焦点/拖拽 = 合成器独占真理源**:client 只发请求收事件,**绝不本地持权威状态**。这样进程内→多进程迁移时 toolkit 不大改。
- **buffer 所有权协议同构**:进程内靠锁/时序隐式,多进程需 `wl_buffer` release 显式协议 + fence。L4a 现在就用显式四态,避免迁移时重写。

这是「现在内核态 / 未来用户态 server 同构」的设计约束,作为**设计目标**而非早期果实(M8 端到端验证)。

## §7 开放问题(实现时定)

- MCU-LCD profile 是否独立于 MCU-Color,还是合并成一个可调档。
- Desktop 子系统(L4b)开关粒度:整组一个宏 vs 每子系统独立宏。
- `visor_host` 表的 RenderBackend 部分是「core 内置 SW」还是「也走表」(影响 GPU 后端插拔方式)。
- Surface 多进程实现的 IPC 协议:Cinux 自研还是对齐 wayland 子集。

> **DRAFT 备注**:分层粒度与边界以 M0 落地的 `visor_host.h` + core 目录结构为准;§5 映射表随重构推进订正。
