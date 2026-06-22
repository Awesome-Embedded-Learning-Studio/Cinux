# F13 visor §4c — dirty + flush 接管(display path 经 Host ABI)

> 2026-06-22 · commit `a3bd518` · run-kernel-test **920 → 927/0**(+7 脏/flush 集成测)
> 分支 `feat/f13-visor`(叠 §4b `24d1dda` 之上,未 push)

## 范围

visor pump 接管 display 路径:renderer 渲 staging back buffer,**display 推送**经
`host->core.flush`(Host ABI),取替 `Canvas::flip()`(直写 framebuffer)。脏区用 §4b Region,
只 flush 变化的 rect(非全屏);空闲帧零 framebuffer 写。**行为不变**(composite 仍全帧重绘
back buffer 恒新),只是 display 推送 host-agnostic。

## 设计:render 与 present 分离

| 旧(§3b) | 新(§4c) |
|---|---|
| `composite()` 渲 back buffer + `flip()`(全帧 back→fb) | `composite()` 仅渲 back buffer(无 flip) |
| pump 每帧 `wm.composite()`(=全帧 flip) | pump:drain→poll→cursor 检查→**dirty 空则 idle 跳过**→否则 composite + 逐 rect flush + clear |
| 直写 framebuffer(visor core 碰 fb) | 经 `host->core.flush` 转发(adapter 持 fb) |

flush 契约([visor_host.h](../../kernel/gui/visor_core/visor_host.h)):`pixels` = staging 缓冲**基址**
(非 rect 左上);`(x,y,w,h)` = 显示坐标里的脏 rect;`stride` = staging 行步长。backend 逐行 copy
到显示(尊重自身 pitch)。**Desktop host MUST 提供 flush**(否则渲了帧不显示,静默冻结)。

## 脏区策略(永不欠覆盖 = 永不丢像素)

`WindowManager::dirty_`(§4b `Region`)+ `invalidate`/`invalidate_all`/`invalidate_cursor_move`:

- **光标移动**(无拖拽):invalidate 旧 + 新光标 footprint(16×16 bitmap + 1px outline + 2px pad)。
  最高频、最小 payload 的 case。composite 仍全帧重绘 back buffer,flush 旧+新 rect 即可(framebuffer
  旧位回退桌面、新位出光标)。✅
- **结构变化**(create/destroy/raise/拖拽):`invalidate_all()`(z-order/曝光,全屏最稳)。
- **terminal 输出**:`poll_output()` 返 bool → 输出则 `invalidate_all()`。
- **无管道 on_key 直写**:on_key 无 stdin pipe 时 `process_char` 直写,`poll_output` 漏掉 → `content_dirty_`
  标志,pump `consume_content_dirty()` → invalidate(生产终端恒带管道,此路 dormant,但补全契约)。
- **首帧**:`last_cursor_` sentinel → `invalidate_all`(整屏首推)。
- **空闲**(无输入/无输出/光标不动):dirty 空 → **不 composite 不 flush**(省 framebuffer 写)。

> 永不过覆盖优先级:脏区**过覆盖=性能代价,欠覆盖=stale-pixel bug**。故结构变化一律全屏,
> 光标 footprint 多 pad。保守 > 精确。

## 4-lens 对抗 review(20 agent,16 findings → 6 confirmed real,全修)

[review workflow](../../.claude/plans/visor-s4c-review.workflow.js) 4 维(flush 转发正确性 / 脏区覆盖 /
pump 控制流 / 回归)+ 对抗验证。6 confirmed:

| # | 严重度 | finding | 修 |
|---|---|---|---|
| 1 | minor | `bytes_per_row=w*4u` 病态 w 溢出(进程内不可达,但 flush 是唯一硬 host 缝) | w/h 钳到 fb 维度(也防溢出) |
| 2 | minor | 无管道 terminal on_key 直写丢帧(§4c idle-skip regression,dormant) | `content_dirty_` 标志 + pump `consume_content_dirty` |
| 3 | minor | (同 #2,另一 lens) | 同 #2 |
| 4 | nit | flush==NULL 渲了不显示(防御,生产恒 set flush) | ABI doc note(Desktop host MUST flush) |
| 5 | nit | 光标检查每帧跑 | **确认正确,不改**(Mouse::x/y 异步变,需输入派发后重评) |
| 6 | **major** | dirty/flush 路径零测试(GUI 测仍直接 composite+flip 绕过 pump) | +7 测(WM 脏区 unit + pump 集成:首帧全屏/idle-skip/invalidate→flush/dirty 清) |

## +7 测([test_visor_dirty.cpp](../../kernel/test/test_visor_dirty.cpp))

- WM 脏区 unit(无 pump/无 Mouse):invalidate 加裁剪 rect / invalidate_all 全屏 / 越界裁剪 / clear。
- pump 集成(fake host flush 记录 rect):**首帧 flush 整屏** / **idle pump flush 0** /
  **invalidate 的 rect 被精确 flush** / flush 后 dirty 清。

> 测试不能驱动光标移动(Mouse 无公开 setter),故光标 footprint invalidate 靠冒烟 + 代码审查 +
  review #5 确认;核心机制(脏区 gating / idle-skip / flush loop)由集成测覆盖。

## 兼容:GUI 测补 `g_screen.flip()`

§4c 前 GUI 测靠 `composite()` 副作用(flip)更新 framebuffer 查 `g_fb.get_pixel()`。现 composite 不 flip,
故 15 处测后补 `g_screen.flip()`(显式 present 供检视),3 处 drag/close/raise 测(handle_mouse 内部
composite 改 invalidate)补 `composite()+flip()`。**测试 inspect 路径与生产 display 路径分离**(测试
用 flip,生产用 flush)——这是 render/present 解耦的正确体现。

## 边界 / GOTCHA

- `composite()` 仍每「脏帧」全帧重绘 back buffer(非脏帧 idle 跳过)。back buffer 恒新;flush 只推脏 rect。
  进一步省(composite 本身只重绘脏区)留 §5 合成器。
- 光标用 `Mouse::x()/y()`(全局,draw_cursor 同源);WM 的 `mouse_x_/mouse_y_` 是拖拽几何用的独立 tracker。
- `visor_pump` 仍 cinux 耦合(用 WM + 读 canvas back buffer);host 提供 staging 的完全解耦留后续。
- `cinux_flush` byte-by-byte(volatile dst MMIO);stride==fb_pitch(同源 init)但代码各读各的(robust)。

## 下一步

- **§4d colorkey→alpha**:icon 编译期 1bpp mask(从 nibble 数据生成,bit=nibble≠0),`draw_bitmap_masked`
  走 mask 废弃 `0x00000000` colorkey(让不透明纯黑可绘——colorkey hack 之前做不到)。
