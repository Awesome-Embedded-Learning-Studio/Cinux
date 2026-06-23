# F13 visor §4d — colorkey → alpha(icon 1-bpp mask)

> 2026-06-22 · commit `9acef71` · run-kernel-test **927 → 930/0**(+3)
> 分支 `feat/f13-visor`(叠 §4c `a3bd518` 之上,未 push)

## 范围

废弃 `Canvas::draw_bitmap` 的「`0x00000000` == 透明」colorkey hack,改**真 alpha**——
独立 1-bpp mask 决定透明。这让**不透明纯黑像素可绘**(colorkey hack 之前:任何
`0x00000000` 都被当透明跳过,纯黑 icon 像素无法存在)。

## 设计:mask 编码 authoring intent,不耦合颜色值

[icon_data.hpp](../../kernel/gui/data/icon_data.hpp) detail 加:

- `IconMask`:32×4 byte(32 行 × 4 byte/行,MSB-first per row)——与 visor `glyph_blit` 的
  mask 约定一致,同一 blitter 可处理两者。
- `build_mask(rows)`:从 `build_icon` **同一份 row-strings** 生成,`mask bit = (nibble ≠ 0)`。

关键:**mask bit 独立于该 nibble 映射的颜色值**。icon 以「nibble 0 = 透明」约定撰写,
mask 把这个 authoring intent 物化成独立通道。于是某个非零 nibble 映射到 `0x00000000`
(纯黑)时,mask bit 仍 set → 绘出(不透明黑)。colorkey 路径下这是不可能的。

`k_shell_rows` / `k_calc_rows` 提为命名 `constexpr` 单一事实源;`k_shell_icon` 与
`k_shell_mask` 同源生成(免重复 32 行字符串)。

## Canvas::draw_bitmap_masked

```
draw_bitmap_masked(x, y, w, h, pixels, mask):
  for row, col: if (mask bit (row,col) set) → 绘 pixels[row*w+col];
                mask == null → 全不透明(全绘)
```

`DesktopIcon` 加 `mask` 字段;`draw_desktop_icons` 走 `draw_bitmap_masked`;`gui_start`
注册 icon 带 `k_shell_mask` / `k_calc_mask`。

## 行为不变(逐像素等价)

现存 icon(palette[0]=BLACK 是唯一透明项):

```
color ≠ 0x00000000  ⇔  nibble ≠ 0  ⇔  mask bit set
```

三者等价 → `draw_bitmap_masked`(mask)与 `draw_bitmap`(colorkey)渲染**逐像素一致**。
mask 只是去掉脆弱的颜色值耦合。**测试 solid icon**(全不透明白/橙,无 `0x00000000`):
`mask=null`(全不透明)与旧 colorkey 路径同。

> §4d 价值是**能力**(纯黑可绘)+**解耦**(透明不由颜色值定),非视觉变化。现存 icon 视觉不变。

## +3 测([test_bitmap_icon.cpp](../../kernel/test/test_bitmap_icon.cpp))

- `draw_bitmap_masked` 只绘 set bit(MSB-first),含 `0x00000000` 被 mask 覆盖时绘出(colorkey fix)。
- `mask=null` 全不透明(纯黑也绘)。
- 越界裁剪(右边界)。

## 边界 / 保留

- `Canvas::draw_bitmap`(colorkey)**保留**:既有 `test_bitmap_icon` 透明度测仍验证它(回归
  保护)。icon 路径已迁到 `draw_bitmap_masked`;`draw_bitmap` 作 legacy primitive 留存。
- `DesktopIcon` 是公共结构(加字段)→ 全量编译验证(绿);host-side 测不碰 GUI 结构,无影响。
- 光标渲染走 `draw_pixel` + 16-bit/行 MSB-first mask(本就是 mask 路径),不受 §4d 影响。

## F13 §4 全域收官(§4a-d)

| 批 | commit | 测试 | 内容 |
|---|---|---|---|
| §4a SwRaster 原语骨架 | `5f2377c` | 899/0(+12) | fill/blit/blit_blend Q8.8/glyph/line+clip,不接管 composite |
| §4b Region 一等 | `24d1dda` | 920/0(+21) | Rect 代数 + bounded Region(永不欠覆盖) |
| §4c dirty+flush 接管 | `a3bd518` | 927/0(+7) | display 经 Host ABI flush,脏区 sub-frame,4-lens review 6 findings 全修 |
| §4d colorkey→alpha | `9acef71` | 930/0(+3) | icon 1-bpp mask,纯黑可绘 |

**visor 绘制引擎 §4 收官**:SwRaster 原语 → Region 代数 → dirty+flush 接管(display host-agnostic)
→ colorkey→alpha。run-kernel-test 887→**930/0**(+43),`make run` 冒烟全程无 panic。
分支 `feat/f13-visor` 共 13 commit 未 push(§1-§4),待用户 push/PR。

## 下一步

§4 全域收官。visor 主体(M0-M9 绘制引擎 widget/合成器)用户独立开发;Cinux 侧 F13 收窄为
「L7 host adapter + 前置重构」。F13 后续 follow-up:dirty lowering(MCU page-band/彩屏 tile)、
合成器 dirty 只重绘脏区(省 composite)、SMP TLB shootdown(CoW 旧页)等。
