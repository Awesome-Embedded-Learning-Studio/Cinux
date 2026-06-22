# F13 visor §3b — Host ABI adapter 接线(visor_pump 经表驱动 GUI,单路径)

> 日期:2026-06-22 · commit `ff462c0` · 分支 `feat/f13-visor`(6 commit 未 push)
> 前置:§1 spawn 公共化(`82e9023`)/ §2 PIT 反转(`0edc70f`)/ §3a Host ABI 骨架(`69cc534`)
> 配套文档:[visor-02-refactor-and-separation.md](../todo/f13-gui/visor-02-refactor-and-separation.md) §3

## 这批做了什么

§3a 只钉了 ABI 边界(头 + 编译期自检),visor core 没有接管。§3b 让 visor core **真正经 Host ABI 函数指针表驱动 GUI**——`visor_pump()` 取事件 / 读时间 / spawn 全走 `host->core.*` / `host->desktop->*`,不直接碰 Mouse 队列 / PIT / framebuffer / process 层。Cinux host adapter 填这张表,转发到现有设施。**行为不变**(adapter 转发,绘制仍走 `wm.composite()` 旧路径;visor 绘制引擎 §4 才接管)。

**单路径(无特性开关)**:visor 的目的就是分离 GUI,所以 visor_pump **直接取代 gui_pump** 成为唯一 pump——`gui_worker_thread` 调 `visor_pump(&cinux_visor_host())`,旧 `gui_pump` 删除(逻辑由 visor_pump 经表等价接管,免双份)。没有 `CINUX_VISOR` 编译开关 / 双路径回退:visor 路径就是 GUI 路径。

## 设计

### 文件布局(全 `kernel/gui/visor_core/`,`CINUX_GUI` 守卫)

| 文件 | 职责 |
|------|------|
| `visor_event_payload.h`(新) | POINTER(18B,kind 区分 move/down/up)/ KEYCODE(3B)packed payload + 修饰位宏 |
| `visor_pump.hpp/cpp`(新) | `visor_pump(host)` 骨架 + `visor_event_to_cinux()` 反序列化 |
| `visor_host_cinux.hpp/cpp`(新) | Cinux adapter:`cinux_visor_host()` 单例 + `cinux_visor_host_init()` 填表;`cinux_poll_event()` 序列化 |

### `visor_pump(host)` 逻辑

1. NULL host → 立即返回;内部读 `now_ms`(NULL-guard,§4 帧率节流用,§3b 暂不用)
2. 经 `host->core.poll_event` drain 事件 → `visor_event_to_cinux` 反序列化回 `cinux::gui::Event` → dispatch WM(`handle_mouse`/`handle_key`)
3. icon action 经 `host->desktop->spawn`(Desktop 非空时),否则 fallback `create_shell_terminal()`
4. terminal poll + `wm.composite()`(§4 绘制引擎接管前走旧绘制)

**关键不变量**:visor_pump body 不直接依赖 Mouse/PIT/framebuffer/process——全经表。换 host 填表即换宿主(=「不感知是否用户态」)。

### Cinux adapter 填表

| 回调 | 转发 |
|------|------|
| `poll_event` | `Mouse::event_queue().dequeue()` → 序列化 `Event`→`visor_event_header`+payload |
| `now_ms` | `(uint32_t)PIT::get_uptime_ms()` |
| `alloc`/`free` | `kmalloc`/`kfree` |
| `log` | `kvprintf`(va_list,`format(printf,2,3)` 属性) |
| `flush` | **占位 no-op**(§3b 死代码:core 不渲染,composite 直画 framebuffer;真实转发留 §4) |
| `flush_complete`/`enter_sleep`/`exit_sleep`/`next_deadline_ms` | NULL(Desktop 同步 flush / MCU 节流才用) |
| Desktop `spawn` | `create_shell_terminal()`(§3b 只支持 shell;通用 spawn(path,argv) 留 §4+) |

### Event ↔ visor_event round-trip

POINTER payload 带 `kind`(move/down/up)——visor ABI 的 POINTER 只有 PRESSED flag,无法表达 cinux WM 依赖的 Move/Down/Up 三态,故 payload 带 kind。KEYCODE 的 pressed 走 header PRESSED flag(press/release 与 key identity 正交),payload 只带 ascii/scancode/modifiers。

## 验证(对齐 DIRECTIVES L5)

- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` → **887/0**(单路径,行为不变)
- `timeout 40 cmake --build build --target run` 冒烟 → `[visor] Cinux host ABI adapter initialised` + `[GUI] desktop composited` + `[PROC] gui_worker` + `[GUI] Worker thread started`,无 panic/FATAL(仅正常 #BP software breakpoint)
- 全量编译零警告(GCC;clangd 的 `-Wmissing-format-attribute` 是 lint 建议非编译门禁,已给 `cinux_log` 加 format 属性消除)
- clang-format 全过

## 对抗 review(4-agent workflow)

commit 前跑 4 维度对抗 review(event round-trip 对称性 / 驱动路径 / GOTCHA 遵循 / ABI 契约)。结论:**0 bug**(reviewer 自验字段映射对称正确、GOTCHA 全 pass、ABI 契约 sound),5 warning + 10 nit。采纳修正:

- **keycode polarity 单源**:serialiser 原从 `ev.key.pressed` 写 flags、switch 却 dispatch `ev.type_`,两者 decouple。改 flags 从 `ev.type_` derive,switch 与 flag 同源。
- **now_ms fold 进 pump**:visor_pump signature 去掉 dead `now_ms` 参数,内部读(带 NULL guard),消除调用方 unguarded deref。
- **host NULL check**:visor_pump 顶部 `if (host == nullptr) return;`,闭合唯一未 guard 的 deref 路径。
- **kind 双射**:deserialiser pointer kind 显式 MOVE case + default reject(原先 garbage→MouseMove)。
- **buf 定长说明**(回应「deserializer OOB」误判):buf 是 visor_pump 定长栈 buffer(tail 24B,memcpy ≤18),绝对安全。加注释说明。
- 其余 nit:EventType switch default 注释、gui_start 注释、PRESSED KEYCODE-only 注释。

## 关于「无特性开关」的决策

初版加了 `CINUX_VISOR` 编译开关(OFF=gui_pump 旧路径 / ON=visor_pump 新路径,双路径并存)。用户指出:**visor 的目的就是分离 GUI,不需要双路径回退开关**——加开关是多余复杂度。故去掉开关:visor_pump 直接成唯一 pump,gui_pump 删除。这符合 visor「分离」的核心目标(代码形状长成 visor 形状就是终点,不是「可切换的实验」)。

## GOTCHA / 教训

- **memcpy 全局,非 `cinux::lib`**:`kernel/lib/string.hpp` 的 memcpy/memset 是 `extern "C"` 全局(GOTCHA#9 同源),初版误写 `cinux::lib::memcpy` 编译失败。
- **`visor_host` 是全局 typedef**(visor_host.h 的 `extern "C"`),不在 `cinux::gui`——init.cpp 误写 `cinux::gui::visor_host&` 编译失败。visor_pump.hpp 在 `cinux::gui` 内引用 `visor_host` 解析到全局,但调用方显式限定要写全局 `visor_host&`。
- **clang-format 拆长注释宏**:visor_event.h 的 PRESSED 宏 + 长行尾注释超列宽,clang-format 拆成 3 行续行(丑)。改「上方注释块 + 短宏定义」避免拆行。
- **flush 死代码**要注释清楚(§4 接管),否则被误判遗漏。
- **删 gui_pump 后 event.hpp 变 unused**(mouse.hpp 间接带)——清理 gui_init.cpp 的直接 include。

## 下一步

**§4 绘制引擎接管**(visor core 接管长弧真正开始):L3 SwRaster(fill_rect/blit/blit_blend/glyph)+ 像素格式硬契约 + Region 一等 + dirty-region invalidation + dirty lowering。届时 adapter 的 flush 从占位变真实转发(core 渲染 staging → host flush),visor_pump 的 composite 改走 SwRaster。

## 文件

- 新:`kernel/gui/visor_core/{visor_event_payload.h,visor_pump.hpp,visor_pump.cpp,visor_host_cinux.hpp,visor_host_cinux.cpp}`
- 改:`kernel/gui/gui_init.{hpp,cpp}`(导出 create_shell_terminal + gui_start 调 cinux_visor_host_init;**删 gui_pump** + 清 event.hpp include)、`kernel/proc/init.cpp`(gui_worker 调 visor_pump)、`kernel/gui/visor_core/{visor_abi_check.cpp,visor_event.h}`(payload 自检 + PRESSED 注释)、`kernel/gui/CMakeLists.txt`(visor sources)
