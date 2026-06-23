# F13 visor §4b — Region 一等(区域代数 + 容量退化策略)

> 2026-06-22 · commit `24d1dda` · run-kernel-test **899 → 920/0**(+21)
> 分支 `feat/f13-visor`(叠 §4a `5f2377c` 之上,未 push)

## 范围

visor L3 的「区域」成为一等公民——dirty 追踪 / 合成器遮挡 / MCU page-band lowering
共用的代数基座。本批只落**数据结构 + 单测**,不碰 composite / flush(§4c 才接管)。

## 交付

| 文件 | 内容 |
|------|------|
| [visor_region.hpp](../../kernel/gui/visor_core/visor_region.hpp) | `Rect`(半开 [x0,x1)×[y0,y1),empty/width/height/contains 点+矩形)+ `rect_intersect/union/translate`(constexpr)+ `rect_subtract`(≤4 条带)+ `Region`(定容,kMaxRects=32)|
| [visor_region.cpp](../../kernel/gui/visor_core/visor_region.cpp) | `rect_subtract`(top/bottom/left/right 四条带,只写非空)+ `Region::add/intersect/translate/subtract/bounds` |
| [test_visor_region.cpp](../../kernel/test/test_visor_region.cpp) | 21 项单测(8 rect + 4 subtract + 9 region) |

## 设计铁律(本批最关键)

**correctness over cleverness——区域决定什么像素上屏,答错即视觉 glitch。**

1. **半开矩形**:`[x0,x1) × [y0,y1)`,与 §4a `ClipRect` 布局一致(可平凡互转)。
2. **退化即空**:`x0>=x1 || y0>=y1` → area 0,contains 永假。`Region` 永不存退化矩形。
3. **永不欠覆盖**(核心):Region 有硬容量 `kMaxRects=32`。**溢出即坍缩为包围盒**——保守过
   近似。区域因此**可能多 flush 没变的像素(性能代价),但绝不漏掉变了的像素(stale-pixel bug)**。
   对 dirty 区:过覆盖是性能问题,欠覆盖是正确性问题。取前者。

## rect_subtract(4-strip 分解)

从 `a` 挖掉 `isect(a,b)` 得最多 4 条带,只写非空的:

```
┌─────────── a ───────────┐
│        top band          │  a.y0 .. isect.y0
├───┬─────────────┬────────┤
│ L │  (carved    │  R     │  isect.y0 .. isect.y1
│   │   out b)    │        │
├───┴─────────────┴────────┤
│       bottom band        │  isect.y1 .. a.y1
└──────────────────────────┘
```

- `b` 与 `a` 不交 → 返 1 个(`a` 原样)。
- `b` 完全覆盖 `a` → 返 0。
- `b` 贴 `a` 边 → 对应条带为空,只返剩余(见 `test_subtract_edge_touch` = 2 条)。

## Region 容量坍缩(非 sticky)

`add`/`subtract` 触发溢出时,整区坍缩为「当前所有成员(+新矩形)的包围盒」,count 归 1。
**非 sticky**:坍缩后继续 add 会再次增长到容量再坍缩(振荡于 1..32,永不超 32)。
因此 `test_region_capacity_collapse` 断言的是**真实不变量**——`count <= kMaxRects`(有界)+
`bounds()` 覆盖全部已加矩形(全跨度不丢)——而非 over-specify 的 `count==1`。

> 实测教训:首版测试写 `count==1`,但非 sticky 坍缩在 37 次 add 后 count=5(坍缩 1 次 + 再
> 追加 4)。修测试断言真实不变量,不改实现——坍缩机制本身正确(有界 + 全覆盖)。

## 边界

- 纯整数(VISOR_NO_FPU 安全)、定长存储(`Rect rects_[32]` + subtract 临时 `Rect staged[128]`)、
  无堆分配、无 host 耦合——可随 visor core 物理分离(§6)。
- `Region` 成员**不保证互不重叠**(`add` 不对已有矩形分裂),是 union-of-rects 近似。够 dirty
  追踪用;精确互斥剖分留 §5 合成器遮挡。
- `Rect` 与 §4a `ClipRect` 布局相同但目前**未统一类型**(免 churn §4a 公共 API);§4c dirty 代码
  直接用 `Rect`,需要时再加 `to_rect(ClipRect)` 互转。

## 下一步

- **§4c dirty+flush 接管**:DirtyRegion(本批 Region)接入 WindowManager,Cinux `flush` 回调
  真实转发(back→framebuffer),visor_pump 经 Host ABI flush 脏区,取替 `Canvas::flip()`。
  composite 渲染逻辑不变(back buffer 全帧重绘恒新),只改 display 推送路径经 ABI。保守脏区
  (结构变化 invalidate 全屏;鼠标移动精确光标矩形;空闲帧不 flush)。
- §4d colorkey→alpha:icon 编译期 1bpp mask,废弃 `0x00000000` colorkey。
