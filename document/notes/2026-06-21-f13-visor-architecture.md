# F13 visor 跨平台 GUI 库 — 七层架构与 ABI 边界(2026-06-21,DRAFT v2)

> ⚠️ **DRAFT / 草稿**。来源同 [research 笔记](2026-06-21-f13-visor-research.md)(11-agent workflow + 对抗验证)。未实现验证,方向可能调整。
>
> **v2 修订(2026-06-21)**:吸收外部审查 `review.md`——① L0 Host ABI **收缩**(核心表 Display/Input/Time/Memory + Desktop extension spawn/rpc,MCU 永远 NULL 的不进核心);② Display ABI 改 **flush 模型**(`begin_frame->pointer` 对 MCU page mode / 用户态 stride-cache-fence 不成立);③ L3 GPU 改 **texture compositor 优先**(primitive draw-list 推迟到有真 GPU);④ L4a **Region 一等** + dirty page-band lowering;⑤ Surface 四态保留但**不早期 Wayland 化**。
>
> 本文给 visor 的**分层架构 + Host ABI 边界 + 设计原则**,并映射 CinuxOS 现有代码。配套:[presets](../todo/f13-gui/visor-01-presets.md)、[refactor](../todo/f13-gui/visor-02-refactor-and-separation.md)。

## §1 七层总览

```
┌─────────────────────────────────────────────────────────────┐
│  应用(Cinux 桌面 shell / MCU 仪表盘 / 未来 GUI app)           │ ← 你写的 UI
├─────────────────────────────────────────────────────────────┤
│ L6  控件工具箱 Widget (Button/Label/Slider/...)   Desktop ◀ VISOR_USE_* 编译期裁剪
├─────────────────────────────────────────────────────────────┤
│ L5  窗口系统 + 合成器 (Surface 四态协议 + dirty-region)         │ ◀ 进程内/多进程同构(不早期 Wayland 化)
├─────────────────────────────────────────────────────────────┤
│ L4b 桌面子系统 (Text排版/矢量/动画/HiDPI/a11y) Desktop-only    │
│ L4a 核心引擎 (Region/脏区/pump/分配器/DPI)  ◀ MCU 可跑          │
├═════════════════════════════════════════════════════════════┤ ← 这条线以上 = visor 库(平台无关)
│ L3  渲染后端 RenderBackend (软件光栅;GPU = texture compositor 优先)│
│ L2  输入后端 + 时间后端 (poll 事件 / now_ms)                   │
│ L1  显示后端 DisplayBackend (flush 模式 + FULL/PARTIAL/PAGE)   │
├─────────────────────────────────────────────────────────────┤
│ L0  Host ABI 表 visor_host.h (核心表 + Desktop extension,唯一硬边界)│ ◀ visor 与宿主接缝
├─────────────────────────────────────────────────────────────┤
│ 宿主:Cinux adapter │ MCU adapter │ 未来 user server │ SDL sim  │ ◀ 各自填表
└─────────────────────────────────────────────────────────────┘
```

**「不感知是否用户态」的实现机制 = L0 Host ABI 表**:visor core 只对这张表说话,换宿主(内核态/用户态/MCU/host)只换表的填充。

## §2 逐层职责与插拔边界

| 层 | 职责 | 插拔边界(换宿主只换这里) |
|---|---|---|
| **L0** Host ABI 表 | `visor_host.h`,**核心表**(Display/Input/Time/Memory/Log)+ **Desktop extension**(spawn/rpc/shared_buffer),freestanding、定宽事件头 | **唯一硬边界**。Cinux=函数转发+填 Desktop ext;MCU=只填核心表(spawn/rpc=NULL);未来 user server=syscall+mmap;SDL=libc。**v2:spawn/rpc 出核心,MCU 永远 NULL 的不占一等表项** |
| **L1** 显示后端 | 像素送屏。**flush 模型(v2)**:`flush(area, pixels, stride, format) + flush_complete`(host→core 通知);三档 FULL/PARTIAL/STREAM-PAGE + 电源状态机 | Cinux=Canvas+flip;MCU 单色=PageBackend(u8g2 picture-loop,1KB);彩屏=SPI DMA;未来=KMS 形状 syscall。**v2:废弃 `begin_frame->pointer`(对 page mode/用户态不稳),core 拥 staging buffer,flush 推送** |
| **L2** 输入+时间后端 | `poll_event()` pull(Pointer/Keycode/Encoder/Touch 四类一等);`now_ms()` 注入时钟 + `next_deadline_ms()`(MCU WFI) | Cinux=转发 Mouse 队列+keyboard;MCU=GPIO/ADC/矩阵;未来=read(evdev)。**ISR 只 push,visor 永远非中断 dequeue** |
| **L3** 渲染后端 | SwRaster(fill/blit/blit_blend 定点/glyph/line)+ clip-rect + transform。**v2:GPU = texture compositor 优先**(widget 软件画到 surface,GPU 只合成/scale/alpha/damage),primitive draw-list 推迟到有真 DMA2D/GLES | MVP=SwRaster(STM32F1 可跑)。**v2:不在早期定 PrimitiveSink+CommandBuffer 双模(会被软件 renderer 绑死);can_render 早期只 All/None** |
| **L4a** 核心引擎 | **Region 一等(v2)**(intersect/union/subtract/translate + 退化策略)+ 脏区管理 + dirty lowering(Desktop rect / MCU page-band / 彩屏 tile)+ `visor_pump(now_ms)` + 分配器 + DPI + Surface 四态协议 | 平台无关,三宿主同一份。**单 GUI 线程无锁**,跨核数据只经 SPSC 队列进 |
| **L4b** 桌面子系统 | Text 排版(shaping 留 M9,**早期只 PSF glyph**)/矢量/动画/HiDPI/a11y | **Desktop-profile-only,MCU nm 零符号**。把「桌面控件库」与「STM32F1」拆开的地方 |
| **L5** 窗口系统/合成器 | Surface 四态(attach/damage/commit/release)+ dirty-region 合成 + 事件路由(**input capture/focus/grab** v2)+ Decorator | 进程内:widget 直画进 Surface;多进程:同接口序列化经 IPC。**v2:四态保留(ownership 对),但不早期 Wayland 化**——z-order/role/IPC/fence/错误模型不进 M4,字段预留 |
| **L6** 控件工具箱 | Widget 树+布局+事件+样式+主题(能力降级) | `VISOR_USE_*` 编译期裁剪。Desktop=虚函数;MCU=CRTP/对象池/**独立 micro renderer**(v2:不继承桌面虚函数 widget 树) |
| **L7** Cinux host adapter | **留 CinuxOS 仓库**,填核心表 + Desktop extension 的 C++ 实现 | spawn 下沉到这(调 launch_user_program)。**误把 kernel 概念漏进 core 就破坏特权中立** |

## §3 设计原则(v2:五条铁律)

1. **特权中立 = pump 模型**。`visor_pump(now_ms)` 主动 dequeue+composite,**绝不在 IRQ 里跑用户函数**。core 永不 busy-wait/epoll/yield/注册 IRQ。
2. **static-only core**。零 `new`/零 STL/`-fno-exceptions -fno-rtti -ffreestanding`/`VISOR_NO_FPU` 定点。CI `nm` 断言零 `__aeabi_*`/`_ZTI`/`__cxa_throw`。
3. **dirty-region only + lowering**。只刷脏块;**但按后端 lowering**(Desktop rect / MCU page-band / 彩屏 tile),不能桌面任意 rect 一刀切(OLED 小 rect = SPI command 风暴)。
4. **profile ceiling + 底座统一(v2)**。真正统一的是底座(Rect/Region/Event/Pixel/Pump/SwRaster)+ 协议 + 资源格式;**不假装同一 widget engine**(MCU 走独立 micro renderer,不继承桌面虚函数树)。
5. **Region 是一等核心(v2 新增)**。`intersect/union/subtract/translate/contains` + **最大 rect 数 + 退化策略**,否则移窗炸 dirty list。

## §4 内核/用户态最小能力 ABI 边界(Cinux 这侧 = F13 实质工作)

7 条(详见 presets §4 ABI 骨架):

| 能力 | Cinux 现状 | 特权中立 ABI(v2) |
|---|---|---|
| 显示访问 | canvas 双缓冲 + flip MMIO | **`flush(area,pixels,stride,fmt) + flush_complete`** + Surface 四态(非 begin_frame->pointer) |
| 输入事件 | Mouse IRQ12→EventQueue,PIT IRQ0 里 dequeue(反模式) | `poll_event`,定宽头+可变尾,ISR 只 push |
| 定时 | PIT tick_callback(承认「只送 1 tick」) | `now_ms` 注入,**不绑死 PIT** |
| pump 驱动 | IRQ 里 composite + worker 只 spawn(两条并存) | `visor_pump→enum{Done,NeedMoreInput,NeedFlushComplete}` + `wake_cb` |
| 进程 spawn | gui_init.cpp 内联(✓ 已公共化为 launch_user_program) | `spawn(path,argv,stdin,stdout)→pid`——**v2:Desktop extension,非核心表** |
| IPC/共享内存 | AddressSpace->map 可用,无 KMS/GPU | `rpc`——**v2:Desktop extension**,对齐 KMS 数据形状不链 libdrm |
| 像素格式+分配器 | 硬编码 uint32 + new[] | 像素格式枚举 + **硬契约**(stride/endianness/premultiplied/byte-bit order)+ `visor_set_allocator` |

## §5 CinuxOS 现有代码 → visor 层映射(重构落点)

| 现有文件 | 当前职责 | → visor 层 | 重构动作 |
|---------|---------|-----------|---------|
| [gui/gui_init.cpp](../../kernel/gui/gui_init.cpp) | GUI 初始化 + shell spawn + PIT tick composite | **拆三处**:spawn→L7(✓);tick composite→L4a pump(**反转,提前**);WM init→L5 | spawn✓;**PIT 反转步骤1(refactor v2 §2)** |
| [gui/window_manager.cpp](../../kernel/gui/window_manager.cpp) | 窗口管理 + 全屏 composite | L5 合成器 | 全屏 composite→dirty-region + Region lowering |
| [gui/window.cpp](../../kernel/gui/window.cpp) | 顶层窗口 | L5 Surface + L6 Window | Window(Surface) vs Widget 解耦 |
| [gui/terminal.cpp](../../kernel/gui/terminal.cpp) | 终端(继承 Window) | L6 Widget(Desktop only) | Terminal→Window 内根 Widget;`VISOR_USE_TERMINAL` |
| [gui/event.hpp](../../kernel/gui/event.hpp) | SPSC 事件队列 | L2 + visor_event | 定宽头+可变尾;`VISOR_EVENT_QUEUE_SIZE` |
| [drivers/canvas.hpp](../../kernel/drivers/canvas.hpp) | 双缓冲绘制 | **拆**:绘制原语→L3 SwRaster;back/front buffer→L1 flush | colorkey→真 alpha(premultiplied);像素格式硬契约 |
| [drivers/video/font.hpp](../../kernel/drivers/video/font.hpp) | PSF2 字体 | L3 glyph + L4b Text(Desktop) | 早期只 PSF;shaping 留 M9 |
| [drivers/mouse.hpp](../../kernel/drivers/mouse.hpp) | PS/2 鼠标 + EventQueue | L2 InputBackend | ISR→visor_event push |

## §6 进程内 → 多进程同构性(Day-1 不变量,v2:不早期 Wayland 化)

visor 按「未来能平滑升级到用户态多进程 server」设计,但 **v2 收紧**:Day-1 只保留 **buffer ownership 不变量**,不复制完整 Wayland:

- **Surface 四态协议(attach/damage/commit/release)**:进程内 commit 后 swap double-buffer;多进程 commit 后所有权移交 server、release 经 IPC 回传。core 同构,迁移点收敛到 host adapter 两实现。
- **z-order/焦点/拖拽 = 合成器独占真理源**:client 只发请求收事件,绝不本地持权威状态。
- **v2 边界**:M4 只做进程内 SurfaceBuffer 双缓冲 + damage + release 回调,**字段对齐未来 IPC 但不设计 RPC 协议**;z-order/role/xdg-shell/fence/错误模型留 M8。

## §7 开放问题(实现时定)

- Desktop 子系统(L4b)开关粒度:整组一个宏 vs 每子系统独立宏。
- texture compositor 接口在 M7 有真 GPU 目标后定(GPU 合成 surface 的精确契约)。
- Surface 多进程 IPC:Cinux 自研还是对齐 wayland 子集(M8 定)。
- (~MCU-LCD 是否独立~ v2 已定:合并进 MCU-Color 可调档)

> **DRAFT v2 备注**:分层粒度以 M0 落地的 `visor_host.h` + core 目录结构为准;§5 映射随重构推进订正。
