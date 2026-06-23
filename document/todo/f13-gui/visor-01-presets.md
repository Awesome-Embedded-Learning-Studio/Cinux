# visor 预设体系 — Profile / 配置宏 / core 约束 / ABI 契约(DRAFT v2)

> ⚠️ **DRAFT / 草稿(2026-06-21)**。基于 visor 跨平台 GUI 库架构调研(11-agent workflow + 3-lens 对抗验证)产出。**未实现验证,用户明确可能调整方向**。落地以最新确认为准。
>
> **v2 修订(2026-06-21)**:吸收外部 AI 审查(`review.md`)的采纳项——① Display ABI 从 `begin_frame->pointer` 改 `flush(area,pixels,stride,format)+flush_complete`(MCU page mode / 用户态 stride-cache-fence 才成立);② L0 Host ABI 收缩(核心 Display/Input/Time/Memory + Desktop extension spawn/rpc,MCU 永远 NULL 的不进核心);③ GPU 改 texture compositor 优先(primitive draw-list 推迟到有真 GPU);④ dirty-region 加 page-band lowering(OLED 不能用桌面任意 rect);⑤ 像素格式 ABI 硬化(stride/endianness/premultiplied/byte-bit order/alignment/cache flush);⑥ profile 合并(MCU-Color 吸收 MCU-LCD 为可调档)。
>
> 本文档定死 visor 的「游戏规则」。配套:[visor-02-refactor-and-separation.md](visor-02-refactor-and-separation.md)(重构执行计划)+ [../../notes/2026-06-21-f13-visor-*.md](../../notes/)(调研/架构/roadmap 结论笔记)。
>
> 取代 2026-05 旧草案([00-gui-abi.md](00-gui-abi.md)/[02-gui-adapter.md](02-gui-adapter.md)/[03-gui-decouple.md](03-gui-decouple.md))。

## §0 设计基线:profile ceiling + 底座统一(必须先接受的现实)

**STM32F103(20KB RAM / 64KB Flash / Cortex-M3 无 FPU)与「Windows/macOS 级完整控件库」不可能用同一份代码同等深度服务。** 业界亦然:LVGL 在 STM32F1 需重度裁剪,u8g2 几乎无控件。

visor 用 **profile ceiling + 底座统一** 诚实拆开(审查 S0.2 深化):
- **真正统一的是「底座」**:`visor-base` = Rect / **Region(一等)** / Event / Pixel / Display flush / Pump / SwRaster 原语。这部分 MCU-F1 到桌面跑同一份。
- **不假装是同一 widget engine**:桌面级能力(富文本 / 矢量 / 动画 / HiDPI / a11y)是 Desktop-only 的 L4b,MCU 永不编译。MCU 的简单 UI 走**独立 micro renderer / static-only 控件**,不继承桌面虚函数 widget 树(虚表 / 样式树 / 事件 bubbling 会爆 64KB Flash)。
- **统一价值来自协议 + 资源格式 + 绘制原语子集**,不是同一棵控件树。

诚实交付预期:**visor 承诺「比现在好看的 Cinux 桌面 + MCU 仪表盘」;完整 macOS/Windows 级是 L4b 长弧**。

## §1 三个 profile 预设(v2 合并 MCU-Color/MCU-LCD)

每个 profile = 一组 `VISOR_*` 宏的预设取值 + 编译/链接约束。一份 `visor_conf.h` 定档整个二进制形态。

| 维度 | **MCU-F1** | **MCU-Color**(吸收原 MCU-LCD 为可调档) | **Desktop** |
|------|-----------|------------------------------------------|-------------|
| 目标硬件 | STM32F103 + SSD1306 单色 OLED | STM32H7/ESP32 + ILI9341,或更大彩屏(色深/缓冲可调) | Cinux 桌面 / host SDL / 未来 user server |
| `VISOR_COLOR_DEPTH` | 1(1bpp) | 16(RGB565)或 8/32(可调) | 32(XRGB8888) |
| `VISOR_BUFFER_MODE` | STREAM-PAGE(逐 8 行) | PARTIAL(1/N 屏) | FULL(整帧双缓冲) |
| 典型显示缓冲 | ~1KB page buffer | 7.5–15KB(1/10~1/20 屏,可调) | 整帧(桌面 MB 级) |
| `VISOR_NO_FPU` | ON(Q8.8 定点) | 看 MCU(Cortex-M3 关 / M4F/M7 开) | OFF |
| `VISOR_ALLOCATOR` | static-pool(无堆) | heap(RTOS)或 static-pool | heap(kmalloc / libc) |
| `VISOR_EVENT_QUEUE_SIZE` | 8(单事件 ~12B) | 16 | 128 |
| 控件 | micro renderer + Label/Button(static-only) | Label/Button/Slider(static-only) | 全 widget(虚函数) |
| `VISOR_USE_TERMINAL` | **OFF**(23.4KiB cell 缓冲死罪) | OFF | ON |
| L4b 桌面子系统 | **全关** | 关 | 全开 |
| 多态实现 | CRTP / 静态分发表 | CRTP / 虚函数(看资源) | 虚函数(接受 vtable) |
| Desktop extension(spawn/rpc) | NULL | NULL | 实现 |

### MCU-F1 profile 详解(最严约束,定义 core 下限)

- **RAM 预算 ≤ 20KB**:page buffer ~1KB + 事件队列 8×12B + 对象池 + 栈。CI 断言 `.bss` + `.data` < 阈值。
- **Flash 预算 ≤ 64KB**:micro renderer + 绘制原语 + 字体子集(xn 数字 / xr ASCII 32-127)。
- **无 FPU**:CI `nm` 零 `__aeabi_*`;渲染全整数 / Q8.8 定点。
- **无堆**:`visor_set_allocator` 注入静态池;core 零 `new`。
- **page renderer**:OLED 走 page-band picture loop(u8g2 范式),**不编译 FULL 全帧 composite 路径**。

### Desktop profile 详解(Cinux 桌面 MVP = 它的内核态后端实例)

- 32bpp + FULL 双缓冲(映射当前 [canvas.hpp](../../../kernel/drivers/canvas.hpp) 的 back/front + flip)。
- 全 widget + L4b(Text 排版 / 矢量 / 动画 / HiDPI / a11y)。
- Desktop extension 实现:spawn(fork→execve + pipe + stdio)。
- 多态用虚函数(对齐 LVGL/GTK,接受 vtable 残留)。

## §2 VISOR_* 配置宏清单(编译期门禁)

照搬 lv_conf.h / u8g2 `U8G2_WITH_*` 范式:**所有裁剪在编译期 `#if` 完成**。CMake option 映射。

**profile 选择**

| 宏 | 取值 | 说明 |
|----|------|------|
| `VISOR_PROFILE` | `MCU_F1` / `MCU_COLOR` / `DESKTOP` | 选定 profile,其余宏未显式设时取该 profile 默认 |

**显示**

| 宏 | 默认(随 profile) | 说明 |
|----|------|------|
| `VISOR_COLOR_DEPTH` | 1/8/16/32 | 像素位深 |
| `VISOR_BUFFER_MODE` | FULL/PARTIAL/STREAM_PAGE | 缓冲/上屏策略 |
| `VISOR_DISPLAY_BUFFER_LINES` | profile 定 | PARTIAL 的 N 分之一、PAGE 的页高(8) |

**渲染 / GPU(v2:texture compositor 优先)**

| 宏 | 说明 |
|----|------|
| `VISOR_NO_FPU` | ON 则渲染全整数/定点(Q8.8),CI nm 零浮点 |
| `VISOR_USE_ALPHA_BLEND` | 真 alpha 混合;MCU-F1 降级为 1bpp alpha-mask 阈值 |
| `VISOR_USE_ANTIALIAS` | 抗锯齿(Desktop);MCU 降级 |
| `VISOR_USE_TEXTURE_COMPOSITOR` | GPU 只合成 surface(scale/alpha/damage),不碰 primitive(Desktop,M7) |

> v2 变更:~~`VISOR_USE_COMMAND_BUFFER`~~ **移出核心宏**。GPU 先做 texture compositor(widget 软件画到 surface,GPU 只合成);primitive draw-list 抽象推迟到有真 DMA2D/GLES 目标后再定,避免被软件 renderer 第一版形状绑死(审查 S1.2)。

**控件(`VISOR_USE_*`,未启用不进二进制)**

`VISOR_USE_LABEL` `VISOR_USE_BUTTON` `VISOR_USE_SLIDER` `VISOR_USE_TERMINAL` ...(逐控件;MCU 用 static-only 子集,Desktop 用虚函数全集)

**布局**

`VISOR_USE_FLEX`(列/行 + row-reverse 为 i18n/RTL 预留)`VISOR_USE_GRID`(Desktop)`VISOR_USE_WRAP`(Desktop)

**L4b 桌面子系统(Desktop-only 整组切,MCU 永不编译)**

| 宏 | 说明 |
|----|------|
| `VISOR_USE_TEXT_LAYOUT` | 富文本排版(TTF + shaping + bidi);**早期 ABI 只承诺 PSF glyph**(审查 S2.2),shaping 留 M9 |
| `VISOR_USE_VECTOR` | 矢量原语(rounded_rect/stroke_path/shadow_blur/clip_path) |
| `VISOR_USE_ANIMATION` | Animation/Timeline(easing + 动画组 + 属性插值) |
| `VISOR_USE_HIDPI` | 分数缩放 |
| `VISOR_USE_A11Y` | 无障碍 AT 抽象层(接 Day-1 hook) |

**内存 / 约束**

| 宏 | 说明 |
|----|------|
| `VISOR_ALLOCATOR` | `static` / `heap` |
| `VISOR_EVENT_QUEUE_SIZE` | SPSC 事件队列槽位(MCU 默认 8) |

**约束开关(铁律)**

| 宏 | 说明 |
|----|------|
| `VISOR_FREESTANDING` | core 强制 `-ffreestanding` |
| `VISOR_NO_EXCEPTIONS` | core 强制 `-fno-exceptions` |
| `VISOR_NO_RTTI` | core 强制 `-fno-rtti` |

## §3 core 编译约束(铁律,跨所有 profile)

1. **`-fno-exceptions -fno-rtti -ffreestanding`**:core 全程。错误用 `Result<T,E>`(no-throw,从 Cinux-Base `ErrorOr` 抽异常无关子集);host adapter 侧才允许 ErrorOr/STL/异常。
2. **零 `new` / 零动态 STL**:禁 `std::vector/map/string`,改静态数组 / intrusive 链 / static-cap 环形。分配走 `visor_malloc/realloc/free` 三指针表。
3. **单 GUI 线程,core 数据结构无锁**。跨核/跨上下文数据**只经 SPSC 队列进入**:ISR/其他核只 push,GUI 线程 pump 只 pop。SMP 多核只输入 enqueue,**绝不并行进 pump**。
4. **`VISOR_NO_FPU` profile 渲染全整数/Q8.8 定点**;CI `nm` 零 `__aeabi_*`。
5. **vtable 保留(Desktop 虚函数),但禁 `dynamic_cast`**。MCU 用 CRTP/静态分发表。
6. **Region 是一等核心类型(v2 新增,审查 S2.3)**:`intersect/union/subtract/translate/contains/is_empty` + **最大 rect 数 + 退化策略**(否则移窗把 dirty list 炸成几十块)。没有退化策略的 Region 不能进 core。

> 与 DIRECTIVES 的关系:visor core 的 `-fno-exceptions -fno-rtti` 与内核一致;`Result<T,E>` 是 `ErrorOr` 异常无关子集;Host ABI 表是「翻译边界」推广到「GUI/内核边界」。

## §4 ABI 契约骨架(visor_host.h / visor_event)

### visor_event — 定宽头 + 可变尾(跨特权 ABI 稳定)

淘汰当前 [event.hpp](../../../kernel/gui/event.hpp) 的裸 `union Event`(24B × 128 = 3KiB):

```c
typedef struct __attribute__((packed)) {
    uint16_t magic;        /* 0x5253 endian/version sanity */
    uint16_t version;      /* VISOR_ABI_VERSION */
    uint8_t  type;         /* POINTER / KEYCODE / ENCODER / TOUCH */
    uint8_t  flags;        /* bit0 pressed; bit1 continue_reading ... */
    uint16_t payload_len;  /* tail byte count; interpretation by type+version */
    /* variable-length payload follows */
} visor_event_header;
```

host 侧 `static_assert(sizeof(visor_event_header) == 8)`;`poll_event` 读头后按 version+payload_len+type 解释尾。MCU 单事件 ~12B。

### visor_host — 核心表 + Desktop extension(v2 收缩,审查 S0.4)

**核心表(所有宿主必填,MCU 也有)**:

```c
typedef struct {
    /* ---- L1 Display backend: flush 模型(v2,非 begin_frame->pointer) ---- */
    /* core 拥有 staging/render buffer;渲染好后把一块矩形像素推给后端上屏。
     * 这取代旧 begin_frame(area)->pointer —— 后者只对 FULL framebuffer 成立,
     * 对 STREAM-PAGE(逐 8 行 page band)/SPI DMA/用户态共享 buffer 都不稳。*/
    void (*flush)(void* ctx, int x, int y, int w, int h,
                  const void* pixels, uint32_t stride, visor_pixel_format fmt);
    void (*flush_complete)(void* ctx);   /* host 通知 core: 上次异步 flush 完成, buffer 可重用 */
    /* ... power state (enter_sleep/exit_sleep), caps ... */

    /* ---- L2 Input backend ---- */
    bool (*poll_event)(void* ctx, visor_event_header* out, uint16_t out_cap);

    /* ---- L2 Time backend ---- */
    uint32_t (*now_ms)(void* ctx);
    uint32_t (*next_deadline_ms)(void* ctx);   /* MCU __WFI */

    /* ---- Memory / Log(所有宿主都有) ---- */
    void* (*alloc)(void* ctx, size_t n);
    void  (*free)(void* ctx, void* p);
    void  (*log)(void* ctx, const char* fmt, ...);
} visor_host_core;

/* ---- Desktop extension(仅 Desktop profile,MCU 永远 NULL,不进核心) ---- */
typedef struct {
    int (*spawn)(void* ctx, const char* path, char* const argv[],
                 int* stdin_fd, int* stdout_fd);
    /* rpc / shared_buffer:M8 多进程 server 时才上,初期 NULL */
} visor_host_desktop;
```

- **v2 关键变更**:`spawn/rpc/shared_buffer` 从核心 5 张表移到 **Desktop extension**——MCU 永远 NULL 的东西不该在唯一硬边界成一等表项(否则"core 不感知宿主"变成"core 知道桌面有进程,只是空")。
- `flush_complete` 方向是 **host→core**(后端通知 core),不是 core 主动调用的普通表函数。
- 宿主启动填 `visor_host_core`(+ Desktop 填 extension)交给 `visor_init`。**visor core 只对表说话,绝不碰 framebuffer/IRQ/syscall/进程**。

### 像素格式 ABI(v2 硬化,审查 S2.4)

`visor_pixel_format` 枚举 + **硬契约**(对齐 Wayland shm 严谨度):

| 属性 | 约定 |
|------|------|
| 格式枚举 | `XRGB8888` / `ARGB8888` / `RGB565` / `1bpp` / `4bpp` / `8bpp` |
| stride | 显式(≠ width*bpp/8,后端可对齐 cache line) |
| endianness | 定(BGRA/XRGB 字节序明示) |
| alpha | **premultiplied**(非 straight);1bpp 是 alpha-mask(非 colorkey,废弃 [canvas.hpp:167](../../../kernel/drivers/canvas.hpp#L167) 的 0x00000000 colorkey) |
| RGB565 byte order | 定( SPI/I2C 屏端序) |
| 1bpp bit order | 定(MSB-first 等) |
| alignment / cache flush | 后端声明;跨用户态需 cache line flush contract |

### dirty-region lowering(v2,审查 S0.3)

Region 不是桌面任意 rect 列表一刀切。flush 时按后端 lowering:
- **Desktop**:rect union / subtract / occlusion(任意矩形)。
- **MCU-F1 OLED**:dirty rect 合并到 **8 行 page band**(小 dirty rect 直接 flush = 大量 I2C/SPI command 切换,更慢;u8g2 picture-loop 范式)。
- **MCU-Color 彩屏**:tile / scanline 分组 + SPI DMA 批量。

DisplayBackend 接收 dirty Region 后自己 lowering 成后端友好的 flush 单位。core 不假设"一块矩形"。

### pump 驱动(特权中立事件循环)

```c
typedef enum {
    VISOR_PUMP_DONE = 0,
    VISOR_PUMP_NEED_MORE_INPUT,
    VISOR_PUMP_NEED_FLUSH_COMPLETE,
} visor_pump_result;

visor_pump_result visor_pump(uint32_t now_ms);  /* timestamp injected by caller */
```

caller(Cinux 内核线程 / MCU main loop / 用户态 epoll)决定何时调。core **永不 busy-wait/epoll/yield/注册 IRQ**;只通过返回值 + host `wake_cb` 表达"需再调度"。

## §5 CI 门禁(v2 加 GUI 测试基建,审查 S2.5)

| 门禁 | 命令(示意) | 作用 |
|------|------------|------|
| 双构建 | host `gcc` + `arm-none-eabi-gcc` | core 真跨平台 |
| 零浮点(MCU-F1) | `nm` 零 `__aeabi_*` | 无 FPU 不泄漏 |
| 零 RTTI/异常 | `nm` 零 `__cxa_throw`/`_ZTI` | freestanding 纯净 |
| core 零 new/STL | `grep -rnE '\bnew\b\|std::vector\|std::map\|std::string' visor/core/` 应空 | static-only |
| 未用控件 gc | `arm-none-eabi-size` N vs N-1,Flash 差 < 1KB | 编译期裁剪 |
| L4b 不进 MCU | MCU-F1 `nm` 零 L4b 符号 | profile ceiling |
| RAM 预算 | `.bss`+`.data` < 20KB(MCU-F1) | 真板可行 |
| **ABI sizeof(v2)** | `static_assert(sizeof(visor_event_header)==8)` 等跨特权 | ABI 稳定 |
| **golden image(v2)** | host SDL 渲染 → pixel CRC 比对基线 | GUI 回归黑洞防护 |
| **event replay(v2)** | deterministic 事件序列回放 | 输入路径回归 |
| **region fuzz(v2)** | Region op 随机输入 + 退化策略验证 | 不炸 dirty list |

## §6 profile 选择规则

- **Cinux 桌面 MVP** → `VISOR_PROFILE=DESKTOP`,host adapter = CinuxOS 仓库内填表。第一个真后端。
- **MCU 验证可移植性** → 先 `MCU-Color`(STM32H7/ESP32 + ILI9341),**MCU-F1 推迟到 M2 真板/真 QEMU RAM<20KB 实测后才承诺**。
- **host 开发主战场** → Desktop + SDL simulator,不刷板子快速迭代。

---

> **DRAFT v2 备注**:profile 划分、宏命名、`visor_host` 精确签名以 M0 落地为淮;texture compositor 接口在 M7 有真 GPU 目标后再定。
