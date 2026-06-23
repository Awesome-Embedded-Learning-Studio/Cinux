# F13 visor — Host ABI 骨架(§3a,2026-06-21)

> F13 §3a。visor Host ABI 边界定义 + 编译期自检。**visor core 未接管**(只钉 ABI 边界)。行为不变。分支 `feat/f13-visor`。配套 [presets §4 v2](../todo/f13-gui/visor-01-presets.md)。

## 产出(`kernel/gui/visor_core/`)

- `visor_event.h`:定宽事件头(magic/version/type/flags/payload_len,packed)**+ 事件类型(Pointer/Keycode/Encoder/Touch)**。淘汰 [event.hpp](../../kernel/gui/event.hpp) 裸 `union Event`(24B 无版本头)。
- `visor_host.h`:**核心表**(Display flush 模型 / Input / Time / Memory)+ **Desktop extension**(spawn)+ `visor_pixel_format` 枚举。v2 关键:flush 模型(非 begin_frame→pointer);spawn/rpc 出核心进 extension(MCU 永远 NULL)。
- `visor_conf.h`:3 profile(MCU_F1 / MCU_COLOR / DESKTOP)+ 默认宏(lv_conf.h 式编译期门禁)。
- `visor_abi_check.cpp`:编译期 ABI 自检(`static_assert(sizeof(visor_event_header)==8)` + packed + aggregate 携带 extension/ctx)。

## 验证(全绿)

- 编译过(visor_abi_check static_assert 触发 + 过)= **ABI sizeof/packed 跨特权机器验证**。
- **run-kernel-test 887/0**(行为不变——visor core 未接管,只加 ABI 定义 + 自检)。
- clang-format 过。

## 设计要点(v2 采纳)

- **核心表 vs Desktop extension**:MCU 永远 NULL 的 spawn/rpc 不进唯一硬边界一等表项(审查 S0.4)。`visor_host` = `visor_host_core`(必填)+ `visor_host_desktop*`(NULL on MCU)+ opaque `ctx`。
- **Display flush 模型**:`flush(area, pixels, stride, fmt) + flush_complete`(host→core 通知),非 `begin_frame→pointer`(对 STREAM-PAGE / SPI DMA / 用户态共享 buffer 不稳)(审查 S0.1)。
- **visor_event 定宽头 + 可变尾**:跨特权 ABI 错配不越界(头是契约,尾按 type+version+payload_len 解释)。

## 下一步(§3b)

Cinux adapter 填表(实现核心表:flush→Canvas+flip / poll_event→Mouse 队列 / now_ms→PIT uptime / alloc→kmalloc;Desktop extension:spawn→launch_user_program 上层)+ `visor_init(host)` / `visor_pump(now_ms)` 骨架 + gui_worker 接 `visor_pump`。visor core 开始经表(先 input 路径),行为不变(adapter 转发 = 直接调)。
