# visor 预设体系 — Profile / 配置宏 / core 约束 / ABI 契约(DRAFT)

> ⚠️ **DRAFT / 草稿(2026-06-21)**。基于 visor 跨平台 GUI 库架构调研(11-agent workflow + 3-lens 对抗验证)产出。**未实现验证,用户明确可能调整方向**。落地以最新确认为准。
>
> 本文档定死 visor 的「游戏规则」:一份代码如何同时服务 STM32F1 单色 OLED ↔ Cinux 桌面 ↔ 未来 GPU。配套:[visor-02-refactor-and-separation.md](visor-02-refactor-and-separation.md)(重构执行计划)+ [../../notes/2026-06-21-f13-visor-*.md](../../notes/)(调研/架构/roadmap 结论笔记)。
>
> 取代 2026-05 旧草案([00-gui-abi.md](00-gui-abi.md)/[02-gui-adapter.md](02-gui-adapter.md)/[03-gui-decouple.md](03-gui-decouple.md),gui_abi.hpp C ABI 方向)——旧方向被「visor 七层 + profile ceiling」取代。

## §0 设计基线:profile ceiling(必须先接受的现实)

**STM32F103(20KB RAM / 64KB Flash / Cortex-M3 无 FPU)与「Windows/macOS 级完整控件库」不可能用同一份代码同等深度服务。** 业界亦然:LVGL 在 STM32F1 需重度裁剪到勉强,u8g2 干脆几乎无控件。

visor 用 **profile ceiling(能力分档)** 诚实拆开:
- 同一份 **core**(L4a 核心引擎 + L1-L3 后端接口)三宿主跑同一份。
- 桌面级能力(富文本排版 / 矢量 / 动画 / HiDPI / 无障碍)是 **Desktop-profile-only 的 L4b 层**,MCU 永不编译(nm 零符号)。
- STM32F1 只承诺 **绘制引擎 + Label/Button 子集**(StaticWidget,对象池固定槽位)。

诚实交付预期:**visor 承诺「比现在好看的 Cinux 桌面 + MCU 仪表盘」;完整 macOS/Windows 级是 L4b 长弧,非 M0-M9 可交付**。

## §1 四个 profile 预设

每个 profile = 一组 `VISOR_*` 宏的预设取值 + 编译/链接约束。一份 `visor_conf.h` 定档整个二进制形态。

| 维度 | **MCU-F1** | **MCU-Color** | **MCU-LCD** | **Desktop** |
|------|-----------|---------------|-------------|-------------|
| 目标硬件 | STM32F103 + SSD1306 单色 OLED | STM32H7/ESP32 + ILI9341 RGB565 | 小色屏(可调,介于 F1/Color) | Cinux 桌面 / host SDL / 未来 user server |
| `VISOR_COLOR_DEPTH` | 1(1bpp) | 16(RGB565) | 8 或 16(可调) | 32(XRGB8888) |
| `VISOR_BUFFER_MODE` | STREAM-PAGE(逐 8 行) | PARTIAL(1/N 屏) | PARTIAL 或 PAGE(可调) | FULL(整帧双缓冲) |
| 典型显示缓冲 | ~1KB page buffer | 7.5–15KB(1/10~1/20 屏) | 可调 | 整帧(桌面 MB 级) |
| `VISOR_NO_FPU` | ON(Q8.8 定点) | OFF(可选,看 MCU 有无 FPU) | OFF(可选) | OFF(可用浮点) |
| `VISOR_ALLOCATOR` | static-pool(无堆) | heap(RTOS)或 static-pool | static-pool 或 heap | heap(kmalloc / libc) |
| `VISOR_EVENT_QUEUE_SIZE` | 8(单事件 ~12B) | 16 | 16 | 128 |
| 控件(`VISOR_USE_*`) | Label/Button 子集(StaticWidget) | Label/Button/Slider 子集 | Label/Button/Slider 子集 | 全开 |
| `VISOR_USE_TERMINAL` | **OFF**(23.4KiB cell 缓冲死罪) | OFF 或降级 | OFF | ON |
| L4b 桌面子系统 | **全关**(Text/矢量/动画/HiDPI/a11y) | 关 | 关 | 全开 |
| `VISOR_USE_COMMAND_BUFFER` | OFF | OFF | OFF | ON(GPU draw-list 预留) |
| 多态实现 | CRTP / 静态分发表 | CRTP / 虚函数 | CRTP / 虚函数 | 虚函数(接受 vtable 残留) |
| spawn(HostProcessBackend) | NULL | NULL | NULL | 实现(fork→execve) |

### MCU-F1 profile 详解(最严约束,定义 core 下限)

- **RAM 预算 ≤ 20KB**:page buffer ~1KB + 事件队列 8×12B + 对象池 + 栈。CI 断言 `.bss` + `.data` < 阈值。
- **Flash 预算 ≤ 64KB**:控件子集 + 绘制引擎 + 用到字体子集。字体子集化到 xn(数字)/xr(ASCII 32-127)。
- **无 FPU**:CI `nm` 零 `__aeabi_fadd/dadd/fmul/...`;渲染全整数/Q8.8 定点。
- **无堆**:`visor_set_allocator` 注入静态池;core 零 `new`;窗口/控件槽位编译期固定数组。
- **绘制引擎 only**:不编译 widget/布局/合成器全屏路径(composite FULL clear+blit 编译期排除)。

### Desktop profile 详解(Cinux 桌面 MVP = 它的内核态后端实例)

- 32bpp + FULL 双缓冲(直接映射当前 [canvas.hpp](../../../kernel/drivers/canvas.hpp) 的 back/front + flip)。
- 全控件 + L4b(Text 排版 / 矢量 / 动画 / HiDPI / a11y)。
- spawn 实现:fork→execve + pipe + stdio 喂回 terminal(当前 [gui_init.cpp](../../../kernel/gui/gui_init.cpp) 的 spawn 下沉到 adapter)。
- 多态用虚函数(对齐 LVGL/GTK,接受 vtable 残留——Desktop 不在乎那点 Flash)。

## §2 VISOR_* 配置宏清单(编译期门禁)

照搬 lv_conf.h / u8g2 `U8G2_WITH_*` 范式:**所有裁剪在编译期 `#if` 完成,运行时配置在 MCU 上是奢侈品**。CMake option 映射到这些宏。

**profile 选择(单选,驱动其余默认值)**

| 宏 | 取值 | 说明 |
|----|------|------|
| `VISOR_PROFILE` | `MCU_F1` / `MCU_COLOR` / `MCU_LCD` / `DESKTOP` | 选定 profile,其余宏未显式设时取该 profile 默认 |

**显示**

| 宏 | 默认(随 profile) | 说明 |
|----|------|------|
| `VISOR_COLOR_DEPTH` | 1/8/16/32 | 像素位深 |
| `VISOR_BUFFER_MODE` | FULL/PARTIAL/STREAM_PAGE | 缓冲/上屏策略 |
| `VISOR_DISPLAY_BUFFER_LINES` | profile 定 | PARTIAL 的 N 分之一、PAGE 的页高(8) |

**渲染 / GPU**

| 宏 | 说明 |
|----|------|
| `VISOR_NO_FPU` | ON 则渲染全整数/定点(Q8.8),CI nm 零浮点 |
| `VISOR_USE_COMMAND_BUFFER` | 保留模式 draw-list(M6+ / Desktop);MCU 编译期关 |
| `VISOR_USE_ANTIALIAS` | 抗锯齿(Desktop);MCU 降级 |
| `VISOR_USE_ALPHA_BLEND` | 真 alpha 混合;MCU-F1 降级为 1bpp alpha-mask 阈值 |

**控件(`VISOR_USE_*`,未启用不进二进制)**

`VISOR_USE_LABEL` `VISOR_USE_BUTTON` `VISOR_USE_SLIDER` `VISOR_USE_TERMINAL` `VISOR_USE_CHECKBOX` ...(逐控件,CMake option 映射)

**布局**

`VISOR_USE_FLEX`(列/行 + row-reverse 为 i18n/RTL 预留)`VISOR_USE_GRID`(Desktop)`VISOR_USE_WRAP`(Desktop)

**L4b 桌面子系统(Desktop-only 整组切,MCU 永不编译)**

| 宏 | 说明 |
|----|------|
| `VISOR_USE_TEXT_LAYOUT` | 富文本排版(TTF + HarfBuzz shaping + FriBidi bidi);MCU 用点阵 glyph blit |
| `VISOR_USE_VECTOR` | 矢量原语(rounded_rect/stroke_path/shadow_blur/clip_path) |
| `VISOR_USE_ANIMATION` | Animation/Timeline(easing + 动画组 + 属性插值 + 状态机) |
| `VISOR_USE_HIDPI` | 分数缩放 |
| `VISOR_USE_A11Y` | 无障碍 AT 抽象层(接 Day-1 hook) |

**内存 / 约束**

| 宏 | 说明 |
|----|------|
| `VISOR_ALLOCATOR` | `static` / `heap` |
| `VISOR_EVENT_QUEUE_SIZE` | SPSC 事件队列槽位(MCU 默认 8) |

**约束开关(铁律,通常不可关)**

| 宏 | 说明 |
|----|------|
| `VISOR_FREESTANDING` | core 强制 `-ffreestanding`(禁 OS/stdlib 依赖) |
| `VISOR_NO_EXCEPTIONS` | core 强制 `-fno-exceptions` |
| `VISOR_NO_RTTI` | core 强制 `-fno-rtti` |

## §3 core 编译约束(铁律,跨所有 profile)

这些是 visor core(`visor/` 非 adapter 部分)的硬约束,profile 无关:

1. **`-fno-exceptions -fno-rtti -ffreestanding`**:core 全程。错误用 `Result<T,E>`(no-throw,从 Cinux-Base `ErrorOr` 抽异常无关子集);**host adapter 侧才允许 ErrorOr/STL/异常**。
2. **零 `new` / 零动态 STL**:禁 `std::vector/map/string`,改静态数组 / intrusive 链 / static-cap 环形。所有分配走 `visor_malloc/realloc/free` 三指针表(默认内置简化 TLSF 或调用方注入)。
3. **单 GUI 线程,core 数据结构无锁**。跨核 / 跨上下文数据**只经 SPSC 队列进入**(EventQueue 沿用):ISR / 其他核只 push,GUI 线程 pump 只 pop。Cinux SMP 多核只负责输入 enqueue,**绝不并行进 pump**(对齐项目 DEBT/concurrency 审计)。
4. **`VISOR_NO_FPU` profile 渲染全整数 / Q8.8 定点**;CI `nm` 零 `__aeabi_*`。
5. **vtable 保留(Desktop 虚函数多态),但禁 `dynamic_cast`**(无 RTTI)。MCU profile 用 CRTP / 静态分发表,确保未用控件 gc 消除。

> 与项目 DIRECTIVES 的关系:visor core 的 `-fno-exceptions -fno-rtti` 与 CinuxOS 内核一致;`Result<T,E>` 是 `ErrorOr` 的异常无关子集(子模块 Cinux-Base 提供);Host ABI 表是 DIRECTIVES「翻译边界」从 syscall trap 推广到「GUI/内核边界」。

## §4 ABI 契约骨架(visor_host.h / visor_event)

### visor_event — 定宽头 + 可变尾(跨特权 ABI 稳定)

淘汰当前 [event.hpp](../../../kernel/gui/event.hpp) 的裸 `union Event`(24B × 128 = 3KiB,无版本头无对齐契约):

```c
/* visor_event.h — fixed-width header + variable-length tail, packed, no padding */
#pragma once
#include <stdint.h>

#define VISOR_EVENT_MAGIC   0x5253  /* 'RS' */
#define VISOR_ABI_VERSION   1

typedef enum {
    VISOR_EVENT_POINTER = 1,   /* mouse / touch: abs+delta + buttons */
    VISOR_EVENT_KEYCODE = 2,   /* keyboard: scancode + ascii + modifiers */
    VISOR_EVENT_ENCODER = 3,   /* rotary encoder: axis diff */
    VISOR_EVENT_TOUCH   = 4,   /* multi-slot touch */
} visor_event_type;

typedef struct __attribute__((packed)) {
    uint16_t magic;        /* VISOR_EVENT_MAGIC for endian/version sanity */
    uint16_t version;      /* VISOR_ABI_VERSION */
    uint8_t  type;         /* visor_event_type */
    uint8_t  flags;        /* bit0: pressed; bit1: continue_reading ... */
    uint16_t payload_len;  /* tail byte count; interpretation by type+version */
    /* variable-length payload follows, read by type+version */
} visor_event_header;
```

- host 侧 `static_assert(sizeof(visor_event_header) == 8)`。
- `poll_event` 读头后按 `version` + `payload_len` + `type` 解释尾;**跨特权 ABI 错配不会越界**(头是契约)。
- MCU 单事件可压到 ~12B(header 8 + payload 4),替代当前 24B union。

### visor_host — 5 张函数指针表(唯一硬边界)

```c
/* visor_host.h — the ONLY hard seam between visor core and host */
typedef struct {
    /* L1 Display backend */
    uint8_t* (*begin_frame)(void* ctx, int x, int y, int w, int h);  /* writable pixels */
    void     (*commit)(void* ctx, int x, int y, int w, int h);
    void     (*flush_ready)(void* ctx, int is_last);
    /* ... power state, caps ... */

    /* L2 Input backend */
    bool     (*poll_event)(void* ctx, visor_event_header* out, uint16_t out_cap);
    /* L2 Time backend */
    uint32_t (*now_ms)(void* ctx);
    uint32_t (*next_deadline_ms)(void* ctx);   /* for MCU __WFI */

    /* L3 Render backend (may be NULL = pure software inside core) */
    /* ... fill_rect / blit / blend + caps query ... */

    /* L7 HostProcess backend (Desktop only, NULL on MCU) */
    int      (*spawn)(void* ctx, const char* path, char* const argv[],
                      int* stdin_fd, int* stdout_fd);

    /* L8 Rpc backend (future multi-process server, NULL initially) */
    /* ... submit(buffer_desc, damage_rects) ... */
} visor_host;
```

- 宿主启动时填这张表交给 `visor_init(const visor_host* host, void* ctx)`。
- **visor core 永远只对这张表说话**,绝不直接碰 framebuffer / IRQ / syscall / 进程结构。
- 换宿主(Cinux 内核态 / 未来用户态 server / MCU 裸机 / host SDL)= 换 5 张表的填充实现。**这就是「不感知是否用户态」的实现机制**。

### pump 驱动(特权中立事件循环)

```c
typedef enum {
    VISOR_PUMP_DONE = 0,            /* nothing more to do now */
    VISOR_PUMP_NEED_MORE_INPUT,     /* ask host to wake when input arrives */
    VISOR_PUMP_NEED_FLUSH_COMPLETE,  /* async flush in flight, wake on done */
} visor_pump_result;

visor_pump_result visor_pump(uint32_t now_ms);  /* timestamp injected by caller */
```

- caller 决定何时何态调用:Cinux 内核线程循环 / MCU main loop / 用户态 epoll 循环。
- core **永不自己 busy-wait / epoll / yield / 注册 IRQ**;只通过返回值 + host `wake_cb` 表达「需再调度」。

## §5 CI 门禁(机器保证 profile 约束)

| 门禁 | 命令(示意) | 作用 |
|------|------------|------|
| 双构建 | host `gcc` + `arm-none-eabi-gcc` | core 真跨平台编译 |
| 零浮点(MCU-F1) | `nm` 零 `__aeabi_*` | 无 FPU 不泄漏 |
| 零 RTTI/异常 | `nm` 零 `__cxa_throw` / `_ZTI` | freestanding 纯净 |
| core 零 new/STL | `grep -rnE '\bnew\b|std::vector|std::map|std::string' visor/core/` 应空 | static-only |
| 未用控件 gc | `arm-none-eabi-size` N 控件 vs N-1,Flash 差 < 1KB | 编译期裁剪有效 |
| L4b 不进 MCU | MCU-F1 profile `nm` 零 L4b 符号 | profile ceiling 落实 |
| RAM 预算 | `.bss` + `.data` < 20KB(MCU-F1) | 真板可行 |

## §6 profile 选择规则

- **Cinux 桌面 MVP** → `VISOR_PROFILE=DESKTOP`,host adapter = CinuxOS 仓库内填表。第一个真后端。
- **MCU 仪表盘验证可移植性** → 先 `MCU-Color`(STM32H7/ESP32 + ILI9341),**MCU-F1 推迟到 M2 真板/真 QEMU RAM<20KB 实测后才正式承诺**。
- **host 开发主战场** → Desktop profile + SDL 后端(simulator),不刷板子快速迭代,是可移植性活证据。

---

> **DRAFT 备注**:profile 划分(尤其 MCU-LCD 是否独立、Desktop 子系统开关粒度)与宏命名是初版,实现时按真板实测调整。`visor_host` 表的精确签名以 M0 落地的 `visor_host.h` 为准。
