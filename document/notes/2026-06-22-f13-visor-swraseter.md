# F13 visor §4a — SwRaster 绘制原语骨架 + 单测

> 日期:2026-06-22 · commit `5f2377c` · 分支 `feat/f13-visor`(8 commit 未 push)
> 前置:§3b Host ABI adapter(`ff462c0`,visor_pump 单路径驱动 GUI)
> 配套:[visor-02 §4](../todo/f13-gui/visor-02-refactor-and-separation.md)、[presets §3/§4](../todo/f13-gui/visor-01-presets.md)

## 这批做了什么

§3b 让 visor core 经 ABI 表驱动 GUI,但**绘制仍走 `wm.composite()`**(Canvas 直画 framebuffer,visor core 没碰像素)。§4 绘制引擎接管是 visor core 接管长弧的真正开始。§4a 是第一步:落 **L3 SwRaster 原语骨架**(纯 CPU / 整数 Q8.8),从 [canvas.cpp](../../kernel/drivers/canvas.cpp) 的 `draw_*` 泛化,**纯新增 + 单测,不接管 composite**——像 §3a 钉 ABI 边界不接管。SwRaster 死代码(除单测)直到 §4c 接管。

## 设计

### `visor::Surface` + `visor::ClipRect`（visor_swraseter.hpp）

- `Surface { void* pixels; uint32_t width, height, stride_bytes; visor_pixel_format format; }`——SwRaster **不拥有 buffer**(调用方拥有:§4c 时 visor_pump staging / host adapter)。`stride_bytes` 显式(后端可对齐 cache line,≠ width×4)。
- `ClipRect { int32_t x0,y0,x1,y1; }`——半开 `[x0,x1)×[y0,y1)`;`nullptr` = 仅 surface bounds。

### 原语（visor_swraseter.cpp,命名空间 `visor::`）

全 clip 到 `clip ∩ surface bounds`,越界静默 no-op(同 canvas `draw_*`):

| 原语 | 泛化来源 | 说明 |
|------|---------|------|
| `fill_rect` | `Canvas::draw_rect` | 实心矩形 |
| `blit` | `Canvas::blit` | 不透明像素拷贝,src 偏移随 dst clip 跟踪 |
| `blit_blend` | 新(Q8.8) | `dst=(src*a+dst*(256-a))>>8`,`a∈[0,256]`,逐通道整数混合 |
| `glyph_blit` | `Canvas::draw_text` glyph 段 | 1bpp alpha-mask(MSB-first/行,bytes_per_row=(gw+7)/8)→ color |
| `draw_line` | `Canvas::draw_line` | Bresenham 全 octant,逐点 clip |

**像素格式**:§4a 只 `VISOR_PIX_XRGB8888`(Desktop),format dispatch(其他 format no-op,留 profile 支持)。**VISOR_NO_FPU 全整数**(presets §3.4,CI `nm` 零 `__aeabi_*`)。

### 单测（test_visor_swraseter.cpp,12 项,run-kernel-test）

`test_visor_fill`(basic / clip_surface / clip_rect)、`test_visor_blit`(basic / partial_clip)、`test_visor_blend`(zero a=0 / full a=256 / half a=128 数值精确)、`test_visor_glyph`(8x2 mask 0xAA/0x0F)、`test_visor_line`(horizontal / clip 负坐标)、`test_visor_stride`(stride=24 padding 列不越界)。

## 验证（对齐 DIRECTIVES L5）

- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` → **899/0**(+12 SwRaster)
- `timeout 40 cmake --build build --target run` 冒烟 → 行为不变(SwRaster 未接管,gui_worker 仍 wm.composite)
- 全量零警告;clang-format 全过

## GOTCHA / 教训

- **`or` 是 C++ 关键字**(`||` 的替代 token):blend 输出通道变量用 `or_`(下划线后缀)避免冲突,`og`/`ob` 不冲突。
- **Q8.8 整数 blend**:禁浮点(VISOR_NO_FPU);`a∈[0,256]` clamp 上界(256=全不透明,超 256 非法);a=0/256 快路径(跳过混合直接 dst/src)。
- **stride ≠ width×4**:SwRaster 用 `stride_bytes`(后端可对齐 cache line),`pixels_per_row = stride_bytes/4`;单测构造 stride=24(6 px/row)、width=4 验证 padding 列不越界。
- **clip 半开区间**:`[x0,x1)`,判断 `< ex1`(不含),避免 off-by-one;同 canvas bounds `< width/height`。
- **blit 的 src 映射**:dst clip 后映射回 src(`src_x = sx + (dst_x - dx)`),部分超出 dst 的列跳过 + src 跟踪(canvas blit 同款逻辑)。
- **format dispatch no-op 而非 assert**:§4a 其他 format 静默 return(非 assert);单测只 XRGB8888 不触发;后续 profile 加 format 时改 dispatch。注:presets §4 说 format 错应 assert(ABI 违例),§4a 保守用 no-op 是因 format 路径未实现(不是违例检测点)。

## 下一步

**§4b Region 一等**:intersect/union/subtract/translate/contains/is_empty + **最大 rect 数 + 退化策略**(presets §3.6,否则移窗把 dirty list 炸成几十块)。纯新增 + 单测(region fuzz presets §5)。之后 §4c dirty+flush 接管(visor_pump 持 staging buffer,SwRaster 渲染 dirty region,adapter flush 真实转发 staging→g_screen back_buf+flip,替换 wm.composite 绘制)、§4d colorkey→alpha(废弃 canvas draw_bitmap `0x00000000` colorkey,icon 资源重导出带 mask)。

## 文件

- 新:`kernel/gui/visor_core/visor_swraseter.{hpp,cpp}` + `kernel/test/test_visor_swraseter.cpp`
- 改:`kernel/gui/CMakeLists.txt`(visor_swraseter.cpp → big_kernel_common)、`kernel/CMakeLists.txt`(test → big_kernel_test)、`kernel/test/main_test.cpp`(forward decl + run_visor_swraseter_tests 调用)
