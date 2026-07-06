# F13-B 花屏根治:dirty 跨帧累积 + flush 负坐标整片跳过

> 2026-07-06,分支 `worktree-f13-gui`(worktree `.claude/worktrees/f13-gui`)。
> 接 [`2026-07-06-f13-b-gui-handoff.md`](2026-07-06-f13-b-gui-handoff.md) 的 Bug ①(脏区残影,当时未诊断)。
> 跟 [`2026-07-06-f13-b-bug2-irq-ist.md`](2026-07-06-f13-b-bug2-irq-ist.md) 互补 —— 那篇修 Bug ②(IST),这篇修 Bug ①(花屏)。

## 现象(用户报告)

快速拖拽窗口时,**该更新的区域不更新**(脏矩形错位),鼠标后续滑过的地方才补着更新。把窗口猛甩出屏幕左/上边界后必触发,且持续不愈。

## 诊断(加仪器,逐步)

静态分析 render 路径无果 —— dirty 走 `rect_union` 累积(过覆盖安全,子模块铁律③),paint 是最新状态,逻辑严丝合缝。按"静态到头就加仪器"方法论,在 `cinux_render_frame` 加临时日志:窗口移动帧打印当帧 dirty rects + 窗口当前/上帧 rect + dirty 并集是否覆盖老新(prevCov/curCov)。

日志演变(铁证)。每帧 `n=2`:一个 cursor 小 rect,一个大 rect(rect2)单调膨胀、只增不减:

| 帧 | rect2 | 说明 |
|---|---|---|
| #956661 | (412,304,613,464) | 窗口老新 bbox,正常 |
| #956788 | (277,61,928,464) | 涨 |
| #956993 | (67,**-10**,1033,732) | y0 变负,窗口拖出顶 |
| #957094 | (**-63**,-10,1033,820) | x0 变负,拖出左;此后永久饱和 |

`prevCov=1` 是**假象** —— 那个全屏 rect 包含一切,`dbounds.contains(prev_win)` 自然 true,但它本身坐标是错的。单看一帧会被骗,看 rect2 跨帧演变直接看穿累积。

## 根因(两个 bug 链式,缺一不可)

### Bug A(子模块,主因):collect_dirty / clear_dirty 不对称

`WindowManager` override 了 `collect_dirty`(递归 `windows_`,因为 `windows_` 不在 `children_`),但**漏 override `clear_dirty`**。基类 `Widget::clear_dirty` 只递归 `children_`(WM 的 children_ 是空!),不清 `windows_` 里的 Window。结果 Window(及其 content)的 `dirty_rect_` **跨帧 `rect_union` 累积,只增不减**。窗口拖出屏幕一次,累积 bbox 永久吃进负坐标,再不缩回。

### Bug B(host,触发,花屏元凶):flush 负 x0 整片跳过

`cinux_flush` 的越界判断:

```cpp
for (row) if (py<0 || py>=fb_h || x<0 || x>=fb_w) continue;  // x 是 rect.x0,固定!
```

`x` 是整个 rect 的 x0(函数参数),不是逐列。一旦 rect.x0<0(bug A 累积出的负坐标),**条件 `x<0` 恒真,每行都 continue,整个 rect 一个像素都不拷**。

### 链式

窗口拖出左边一次 → bug A 让累积 dirty 的 x0 永久负 → bug B 让每帧窗口大片 dirty rect 整个跳过 → 该更新的不更新 = 花屏。cursor 的小 rect(正坐标)正常 flush → "**鼠标滑到哪才更新**"。

## 为什么静态分析打脸

bug A 本该是**过覆盖**(dirty 变大 = flush 多 = 慢但正确)。但 bug B 把"负 x0 的过覆盖 rect"反转成"整片跳过" = 欠覆盖。子模块铁律③说的 stale-pixel,杀手不是 Region 代数(那确实永不欠覆盖),是 flush 层的负坐标处理。**上游的覆盖保证,被下游一个坐标 bug 直接废掉。**

## 根治

| 文件 | 改动 |
|---|---|
| [window_manager.hpp](../../third_party/Cinux-GUI/core/widget/window_manager.hpp) | 加 `clear_dirty()` override 声明 |
| [window_manager.cpp](../../third_party/Cinux-GUI/core/widget/window_manager.cpp) | `clear_dirty` 实现:清自己 + 递归 `windows_`(镜像 `collect_dirty`) |
| [host_cinux.cpp](../../kernel/gui/host_cinux.cpp) | `cinux_flush` 入口 clamp 负 x/y 到屏内(x<0 → w+=x, x=0),右下越界也 clamp;循环不再 per-pixel 检查 |

## 验证

用户 GUI 实测:快速拖拽 + 猛甩出屏幕左/上,**花屏消失**,窗口跟手、老位置干净恢复,不用鼠标回扫。日志 rect2 回归窗口大小级小矩形(正坐标),不再有全屏负 bbox。

## ⭐ 教训 / GOTCHA

1. **collect/clear 不对称是隐形 bug**。override 了递归收集(`collect_dirty`),就必须 override 递归清理(`clear_dirty`)。两个方向对称,漏一个就单向往一边累积。同族:任何"递归收集 + 递归清理"的成对钩子。
2. **"过覆盖安全"依赖下游正确**。Region 代数保证过覆盖(铁律③),但 flush 层一个负坐标 bug 就把过覆盖反转成欠覆盖。整条 dirty→render→flush 管线,任何一个把 rect 坐标搞错的环节,都会破坏上游的覆盖保证。
3. **flush 边界判断用 rect.x0 做逐行条件是错的**。x0 是 rect 属性(固定),逐行条件该用逐列 py/px,或干脆入口 clamp 后无脑拷。负坐标 clamp 入口最简、最不易错。
4. **日志仪器看演变,不看单帧**。单看一帧 `prevCov=1` 会误判"覆盖正常"。看 rect2 跨帧单调膨胀(412,304 → 277,61 → 67,-10 → -63,-10)直接看穿"只增不减"的累积。

## follow-up

- 子模块 upstream:cursor fix + `clear_dirty` + 撤 debug trap 搬 `~/Cinux-GUI` commit + push + worktree bump pin。
- F13-B 批2:PTY 真终端(`TerminalSession` + `HostDesktop::spawn` fork+execve `/bin/sh` + PTY slave,复用 F10-M3 PTY 框架)。
