# F13 visor 跨平台 GUI 库 — 调研结论与对抗验证(2026-06-21)

> ⚠️ **DRAFT / 草稿**。来源:11-agent workflow(5 维并行调研 + 综合 + 3-lens 对抗 + 完整性补强 + 收敛)。未实现验证,方向可能调整。
>
> 调研对象:把 CinuxOS 现有内核内联 GUI(~3300 行)抽成独立跨平台库 **visor**,同一份 core 跑 Cinux 桌面(现内核态 / 未来用户态)、单片机 LCD/OLED(小到 STM32F103)、未来显卡。用户 4 决策:① 完整控件工具箱 ② Cinux 桌面先 ③ MCU 全规模含 STM32F1 ④ GPU 可插拔先软件。

## §1 嵌入式 GUI 先例借鉴(LVGL v9 / u8g2 / uGUI)

| 先例 | visor 借鉴点 |
|------|-------------|
| **LVGL pump 模型** | 主循环 `lv_timer_handler()` 主动刷新,`lv_tick_set_cb` 注入时间源。**核心从不假设跑在 IRQ 还是用户态**——正是 visor 特权中立 ABI 要的。visor 化时把 CinuxOS 当前「PIT IRQ0 里 dequeue+composite」改成「主循环调 `visor_pump()`,IRQ 只 enqueue 输入」。 |
| **flush_cb 显示边界** | `void flush_cb(disp, area, px_map)` + `lv_display_flush_ready()` 完成确认。后端不知库怎么渲染(软件/GPU),库不知后端是 SPI-OLED 还是 framebuffer。**visor::DisplayBackend 接口 = 这个签名**。 |
| **partial buffer / invalidation area** | LVGL 三模式 PARTIAL(<屏 buffer,只刷脏区,1/10 屏)/ DIRECT / FULL。**PARTIAL 是 MCU 20KB RAM 救命设计**:320×240 RGB565 全屏 150KB,1/10 屏只要 15KB。CinuxOS 现状全帧 flip(桌面勉强,MCU 不可行)。 |
| **脏矩形驱动** | 控件 `lv_obj_invalidate` → 周期合并/拆分脏区 → 只重画脏区。CinuxOS 当前 `composite()` 每 tick 全屏 clear+blit 是 MCU 性能灾难。 |
| **indev 抽象** | POINTER / KEYPAD / ENCODER / BUTTON 四类 + `read_cb` 函数指针。visor InputBackend = `poll(InputEvent&)`,内核态(IRQ enqueue)/用户态(syscall)/MCU(GPIO)实现同接口。 |
| **malloc 钩子 + lv_conf.h 编译期裁剪** | `LV_MEM_CUSTOM` 注入分配器;逐 widget/特性 `LV_USE_*` 宏,未启用不进二进制。u8g2 实测关 3 特性省 8% Flash。**visor 照搬:`visor_set_allocator` + `VISOR_USE_*` + `visor_conf.h`**。 |
| **u8g2 page buffer / picture loop** | 单色 OLED 逐 8 行重画,DRAM ~1KB。STM32F1 极限场景范式。visor STREAM-PAGE 档 = 它。 |
| **simulator = 后端换 SDL,core 零改动** | LVGL PC simulator 同一份 UI 代码 SDL 跑 / MCU SPI 跑。**证明「core 平台无关 + 后端可插拔」工业可行**。visor simulator 一等公民。 |

## §2 桌面窗口系统借鉴(SerenityOS / Wayland / DWM)

| 概念 | 借鉴 |
|------|------|
| **client-server 合成模型** | widget 自己生成最终像素,server/compositor 只做 z-order 合成/混合/特效,**不暴露 DrawLine 高层原语**(否定 X11 双重渲染)。visor L5 合成器照此。 |
| **Surface + dirty-region 合成** | SerenityOS Compositor / Wayland damage:back-to-front 只重绘脏区可见像素,flip 只更新脏 scanlines。废弃 CinuxOS 全屏 composite。 |
| **Decorator 可插布** | 标题栏/关闭/resize 的 `paint()` 与 `hit_test()` 同实例负责(CinuxOS 当前 `draw_title_bar` 与 `is_close_button_hit` 分散不一致)。 |
| **Widget ≠ Window** | SerenityOS LibGUI:Widget 是控件树,Window 是顶层 Surface。修正 CinuxOS「Terminal 继承 Window」的耦合(控件=窗口)。 |
| **进程内 / 多进程同构** | Wayland `wl_surface` 四态(attach/damage/commit/release):进程内 commit 后 swap double-buffer;多进程 commit 后所有权移交 server、release 经 IPC 回传。**core 同构,迁移点收敛到 host adapter 两实现**。 |

## §3 内核最小能力 + 特权中立(DRM/KMS / evdev)

- 内核为 GUI 提供最小能力:**显示内存访问**(mmap fb / DRM dumb buffer / KMS atomic commit)+ **输入事件流**(evdev / PS2 / hidraw)+ **定时** + **进程 spawn** + **IPC 共享内存**。
- **pump 模型特权中立**:同一 ABI,内核态 adapter 直接函数转发,用户态 adapter 走 syscall+mmap。visor_pump(now_ms) 时间戳调用方注入(等价 lv_tick_set_cb)。
- **Cinux 特例**:Cinux 自己是内核有 VBE fb,**不走用户态 DRM/KMS ioctl**。砍掉真 libdrm/KMS 后端,改成「KMS 形状的 syscall 后端」(dumb-buffer mmap + page flip,Cinux 加 2-3 syscall);visor 接口只对齐 atomic_commit + FB_DAMAGE_CLIPS **数据形状**(buffer 句柄 + damage 矩形列表),不引入 libdrm 依赖。

## §4 GPU 后端分级(L0 软件 → L3 3D)

| 级 | 能力 | visor 实现 |
|----|------|-----------|
| L0 | 纯软件光栅(CPU 像素,整数/定点) | **SwRasterBackend(MVP 唯一)**,STM32F1 可跑 |
| L1 | 2D blit/fill 引擎 | 未来 DMA2D |
| L2 | GPU 2D 加速 | Gpu2DBackend(模拟 DMA2D,M7) |
| L3 | 3D API(纹理合成) | GpuTextureBackend(GLES/Vulkan 录命令) |

- **draw-list / CommandBuffer 抽象**:core 发绘制命令,backend 决定软件或 GPU 执行。接口现在定义,实现先只交付 SW。**软件→GPU 不是后端平移**:即时模式(逐像素)vs 保留模式(批提交)。→ RenderBackend 分两层:PrimitiveSink(即时)+ CommandBuffer(可选)。M1 Dispatcher 二选一(全软件/全录),逐原语混分推迟 M7。
- **Capabilities 透明回退陷阱**:逐原语回退可能比纯软件还差(DMA2D 性能悬崖)。→ `can_render(draw_list)->{All,Partial,None}` 整段判定;Partial 默认全软件(安全),仅 host opt-in 混合;加 per-frame 硬件加速命中率统计(对齐「可观测优先于性能」)。

## §5 STM32F1 可行性结论(关键诚实判断)

- **LVGL README 最低需求:32KB RAM + 128KB Flash + 1/10 屏缓冲**。STM32F103C8(64KB Flash / 20KB RAM)**低于门槛**。
- 社区实践:STM32F103 跑 LVGL 需把 `LV_MEM_SIZE` 压到 ~8KB、PARTIAL 单缓冲、仅几个 label/button、关动画阴影,**非常勉强**;更常见用更大 RAM 的 VE/ZG 或退到 u8g2。
- **结论**:visor 在 STM32F1 级(20KB RAM)真正可用,**必须提供 u8g2 式 page-buffer / 极简路径,不能只依赖 LVGL 式堆分配 widget 树**。→ core 分层:绘制原语(u8g2 式极简,MCU-F1 可跑)+ 可选 widget/布局层(仅桌面和富 MCU)。
- 这直接导出 **profile ceiling** 设计(见 presets §0):承认跨度不可能同深度服务。

## §6 对抗审查揪出的裂缝(针对 Cinux 现有代码)

3 lens(STM32F1 杀手 / 桌面完整性 / GPU+特权中立)找出 ~20 条,以下是**针对 Cinux 现有代码、必须在重构时处理**的关键项:

| # | 裂缝(代码定位) | 严重度 | 处置 |
|---|----------------|--------|------|
| 1 | [canvas.hpp](../../kernel/drivers/canvas.hpp) `back_buf_=new uint32_t[total_pixels]`,640×480=1.2MiB | STM32F1 死罪(超标 60 倍) | FULL 路径 MCU profile 编译期排除;MCU-F1 只 PageBackend |
| 2 | [event.hpp:143](../../kernel/gui/event.hpp#L143) `BUF_SIZE=128` 硬编码,Event 24B×128=3KiB(占 F1 15% RAM) | 高 | `VISOR_EVENT_QUEUE_SIZE` 宏(MCU 默认 8);visor_event 定宽头+可变尾压到 ~12B |
| 3 | [terminal.hpp:290](../../kernel/gui/terminal.hpp#L290) `screen_[25][80]` cell 12B=23.4KiB | 单块 KO STM32F1 | Terminal 是桌面控件,`VISOR_USE_TERMINAL` 宏,MCU profile 关;MCU 用 StaticText |
| 4 | [window_manager.cpp:183-205](../../kernel/gui/window_manager.cpp#L183-L205) `composite()` 每 tick 全屏 clear+blit | MCU 分水岭 | dirty-region 最小版前置 M1;MCU 排除 FULL clear/blit 路径 |
| 5 | [gui_init.cpp:94-159](../../kernel/gui/gui_init.cpp#L94-L159) 与 [init.cpp:81-140](../../kernel/proc/init.cpp#L81-L140) shell 启动**高度重复** | 迁移起点就违约「不重写」 | **M0 前置重构**:合并成单条 fork→execve 公共路径,顺修 GOTCHA#22 tid 污染 + F2-M5 Stack VMA 三处合一 |
| 6 | [gui_tick_callback](../../kernel/gui/gui_init.cpp#L260) 在 PIT IRQ0 里 dequeue+composite | IRQ 跑渲染=雷区 | M3 反转:worker 线程接管全部,PIT ISR 退役或仅 enqueue |
| 7 | [gui_init.cpp:355-358](../../kernel/gui/gui_init.cpp#L355-L358) 注释承认「APIC 只送 1 PIT tick」被 composite 预绘 workaround 掩盖 | 可靠性债 | M3 反转后转为正向验收点(即使 PIT 不送达 GUI 仍刷新,靠 Scheduler::yield+now_ms 节流) |
| 8 | [canvas.hpp:167-168](../../kernel/drivers/canvas.hpp#L167-L168) `draw_bitmap` 用 `0x00000000` colorkey 当透明 | 丢纯黑像素,语义错 | M1 改真 alpha(独立 1bpp alpha mask),icon 资源重导出 |
| 9 | 无 FPU 下真 alpha 全屏 blend 5.3ms 算术 | Cortex-M3 fps 崩塌 | core 钉死 `VISOR_NO_FPU` + Q8.8 定点;CI nm 零 `__aeabi_*`;MCU-Color 默认关 alpha blend |
| 10 | `gui_init.cpp` / Window 全 `new` | 与 static-only 冲突 | visor core 零 new(grep+CI 门禁);堆只在 host adapter 侧;MCU 对象池注入 |

## §7 完整性缺口(对抗外补强)

ABI 版本化与跨特权 sizeof/对齐契约 · core 异常/RTTI/STL 关闭边界 · MCU 无堆 vs 控件树虚函数(对象池) · visor↔CinuxOS 代码流动(submodule + add_library 重编译) · 电源/休眠(DisplayBackend 电源状态机 + next_deadline_ms 供 WFI) · DPI/缩放(logical↔physical) · 并发/线程模型(单 GUI 线程铁律) · 输入设备多样性(编码器/矩阵键盘/ADC 触摸升一等) · 初始化序列 + 多屏热插拔 · 资源 alpha 通道统一 · a11y(Day-1 hook) · i18n/RTL(UTF-8 + bidi + row-reverse)。详见 [presets](../todo/f13-gui/visor-01-presets.md) §4-§5 与 architecture 笔记。

## §8 GOTCHA / 复用陷阱

- **桌面控件库 ↔ STM32F1 是两个 ceiling**:别指望一份代码同等深度。用 profile ceiling 拆,诚实承诺分层。
- **软件→GPU 不是后端平移**:即时 vs 保留模式。RenderBackend 分两层,接口现在定义,实现先只 SW。
- **pump 反转不干净有两条路**:确认 `gui_worker_thread`(init.cpp:29-34)现仅 spawn,真正 composite 在 PIT IRQ0。反转要把 worker 升级为 pump 循环,不是只搬 composite。
- **spawn 公共化是 visor 前提**:gui_init.cpp 与 init.cpp 已重复,不先合并,L7 adapter 就是重写,且是迁移最易回归点。
- **KMS 在 Cinux 内核态用不上**:别引 libdrm,只对齐 KMS 数据形状,内核加 2-3 syscall。

> **DRAFT 备注**:调研先例版本/数字以 workflow agent 检索为准,落地前对关键数字(如 LVGL STM32F1 RAM 占用、u8g2 feature 裁剪比例)做二次核对。
