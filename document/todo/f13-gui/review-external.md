结论先砍一刀：**方向大体对，但现在的方案把“跨平台库”说成了“同一套 GUI 系统”。这会失败。** 真正能统一的是：坐标/区域/像素格式/事件/pump/显示 flush/少量绘制原语。不能统一的是：STM32F1 的 UI 模型、桌面窗口系统、未来 GPU draw-list。它们应该共用底座，不应该假装是同一层级的同一个 toolkit。

我看了当前仓库计划和实现：`PLAN.md` 仍说 spawn 公共化是第一步，但 `git log` 最新已有 `82e9023 refactor(proc): 提取 launch_user_program 公共化用户程序启动`，所以后续审查按“spawn 前置已基本发生，下一刀是 GUI pump/调度模型”来判断。

**S0 / 会失败**
1. **`begin_frame(area) -> pointer` 这个 Display ABI 不成立。**  
   对 FULL framebuffer 可以，对 STREAM-PAGE/1bpp OLED/SPI DMA/未来用户态共享 buffer 都不稳。1bpp 页模式不是“给你一块矩形像素内存随便写”，而是 page band picture loop；用户态 server 还涉及 stride、cache、fence、ownership。  
   建议：Display 侧改成更接近 `flush(area, pixel_span, stride, format)` + `flush_complete` 异步完成；RenderTarget/staging buffer 由 core 或 display backend 显式拥有。`flush_ready` 方向也要反过来：这是 host/driver 通知 core，不应像 core 主动调用的普通表函数。

2. **STM32F1 profile 不能跑“同一个 widget core”的低配版。**  
   如果桌面 Button/Label 共享样式树、状态机、布局、事件 bubbling、主题降级，64KB Flash/20KB RAM 会被虚表、style property、字体、队列、对象池吃爆。若反过来为 F1 约束桌面，桌面会永久停在玩具水平。  
   建议拆成三个实包：`visor-base`（Rect/Region/Event/Pixel/Display/Pump/SwRaster）、`visor-micro`（page renderer + static scene + Label/Button 极简）、`visor-desktop`（Widget/Window/Text/Theme）。统一的是协议和资源格式，不是完整控件树。

3. **dirty-region 对 F1 不是银弹，错误实现会更慢。**  
   OLED/page buffer 场景下，很多小 dirty rect 会导致大量 I2C/SPI command 切换；你需要的是 dirty page bands / tile scheduler，而不是桌面式任意矩形列表。  
   建议：Region API 底层必须支持 profile-specific lowering：Desktop 用 rect union/subtract/occlusion；F1 把 dirty 合并到 8 行 page band；彩屏可 tile/scanline。

4. **Process/Rpc 放进核心 Host ABI 是污染。**  
   MCU 永远 NULL 的东西不该在 core 的唯一硬边界里成为一等表项。否则所谓“core 不感知宿主”会变成“core 知道桌面有进程和 RPC，只是有时为空”。  
   建议：L0 只保留 Display/Input/Time/Memory/Log 或 Debug。`spawn`、`rpc`、`shared_buffer` 是 DesktopHost extension。

**S1 / 高风险**
1. **Surface 四态 Day-1 不是错，但 Wayland 化过早。**  
   `attach/damage/commit/release` 很值得保留，因为它把 buffer ownership 讲清楚。可是 z-order、role、xdg-shell、IPC、release fence、client/server 错误模型，现在都不该进 M0-M4。  
   建议：先实现进程内 `SurfaceBuffer` 双缓冲 + damage + release 回调，结构字段对齐未来 IPC。不要现在设计完整 RPC 协议。

2. **GPU 抽象现在定型太早。**  
   `PrimitiveSink + CommandBuffer` 听起来对，但如果没有真实 GPU backend，命令集会被软件 renderer 的第一版形状绑死。成熟库也不是简单“每个 primitive 一个函数”：LVGL 是 draw context callback；Skia 是复杂 display list + paint model；pixman 更偏 region/composite；Blend2D 对 freestanding 内核不现实。  
   建议：第一阶段别承诺 GPU 可插拔到 primitive。先把未来 GPU 定义为 **texture compositor**：窗口/控件仍软件画到 surface，GPU 只合成 surface、scale、alpha、damage。等有 DMA2D/GLES 目标后，再抽 draw-list。

3. **`can_render(All/Partial/None)` 方向对，但 Partial 默认回退会踩性能坑。**  
   CPU 画半帧、GPU 画半帧，加同步和 cache flush，可能比纯 CPU 慢。  
   建议：M7 以前只允许 `All` 或 `None`。`Partial` 需要显式 opt-in + 每帧统计：命中率、fallback 次数、flush wait 时间。

4. **PIT 反转顺序建议提前。**  
   当前 `gui_tick_callback` 里做 dequeue、terminal poll、render、full composite，且注释承认 production GUI 只收到 1 个 PIT tick 后靠预绘 workaround。这个比 SwRaster 抽象更危险。  
   建议下一刀：先用现有 Canvas/WindowManager 做 `gui_worker_thread -> gui_pump()`，把 IRQ composite 去掉；行为不变后再抽 RenderBackend/dirty-region。也就是：调度模型先正确，绘制抽象后正确。

**S2 / 设计缺口**
1. **事件系统缺 input capture/focus/grab。**  
   拖窗、按钮按下后移出、弹窗、菜单、滚动条，都需要 pointer capture；键盘需要 focused widget、repeat、modifier、compose/IME 预留。

2. **文本管线低估了。**  
   桌面只要出现 UTF-8、CJK、fallback font、selection、光标移动，PSF glyph blit 就不够。建议 M5 只承诺 ASCII/PSF terminal；M9 再引 shaping/cache/fallback。不要把 “TTF+HarfBuzz+FriBidi” 写进早期 ABI。

3. **Region/clip 是核心，不是附属。**  
   必须有 `intersect/union/subtract/translate/contains/is_empty`，还要有最大 rect 数和退化策略。没有退化策略，移动一个窗口可能把 dirty list 炸成几十块。

4. **像素格式 ABI 要更硬。**  
   需要 stride、endianness、premultiplied alpha、color key 禁止、RGB565 byte order、1bpp bit order、alignment、cache line flush contract。Wayland 的 shm 格式明确到 stride/format/premultiplied alpha 这一层，你也要有类似严谨度。

5. **测试策略还不够。**  
   加 SDL golden image / pixel CRC / deterministic event replay / region fuzz / ABI sizeof static_assert / MCU `.bss+.data` budget / `nm` 浮点和 RTTI 门禁。GUI 没这些，会变成“看起来能跑”的回归黑洞。

**逐条回答**
1. **跨度策略**：profile ceiling 是对的，但要改口径：visor 在 F1 上不是“完整库降级”，而是“同协议的 micro renderer”。统一价值来自同一资源格式、同一 event/display ABI、同一绘制原语子集，而不是同一 widget engine。

2. **特权中立 ABI**：函数指针表能隐藏调用位置，不能自动隐藏内存所有权、并发、安全、cache/fence。它可以成立，但 ABI 必须从“5 张大表”收缩成小核心 + desktop extension。多进程窗口未来需要，但 Day-1 只保留 surface ownership 不变量。

3. **GPU 演进**：先 texture compositor，后 primitive acceleration。不要 M0 定 CommandBuffer。可参考 LVGL draw context callback 的思路，但别复制它的对象/style 模型；Skia/Blend2D 太重，pixman 的 region/composite 语义更值得借鉴。

4. **重构顺序**：spawn 前置已经有进展；下一步建议改为：`worker pump 反转 -> Host ABI 空壳 -> dirty over existing Canvas -> SwRaster 抽取 -> Surface/Widget`。PIT 反转最大风险是黑屏、输入饥饿、terminal poll 阻塞、worker 优先级不够；用 wake/deadline/frame budget 控住。

5. **遗漏**：input capture、region 退化、stride/cache/fence、font cache、IME/UTF-8、multi-output/DPI/rotation、client death、resize lifecycle、power/backpressure、golden tests。这些比 a11y/动画更早、更硬。

6. **过度设计**：7 层太像论文。建议砍成 4 块：`host/adapters`、`draw/display core`、`ui toolkit`、`desktop/window server optional`。砍掉 M0 的 Rpc/Process/CommandBuffer，合并 MCU-Color/MCU-LCD，保留 Surface 四态的最小结构。

对标参考：LVGL 官方文档明确不建议在 interrupt handler 调 LVGL API，除 tick/flush-ready 外应设 flag 后由 timer/pump 处理；这支持你反转 PIT composite 的方向。Wayland 的 `wl_surface attach/damage/commit` 和 `wl_buffer release` 证明显式 buffer ownership 是正确抽象，但不是要求你现在复制完整 Wayland。u8g2 的 page-buffer/picture-loop 也说明 F1 路径必须是 page renderer，而不是小号桌面 framebuffer。  
Sources: [LVGL OS/interrupts](https://lvgl.io/docs/open/8.3/porting/os.html), [LVGL custom GPU/draw context](https://lvgl.io/docs/open/8.3/porting/gpu.html), [Wayland protocol](https://wayland.freedesktop.org/docs/html/apa.html), [u8g2 reference](https://github.com/olikraus/u8g2/wiki/u8g2reference).