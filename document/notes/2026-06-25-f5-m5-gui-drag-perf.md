# F5-M5 GUI 拖拽卡顿修复(免冗余终端重渲)— 2026-06-25

> 分支 `feat/f5-m5-xhci-3`,接 [2026-06-24-f5-m5-tablet-cursor-sync.md](2026-06-24-f5-m5-tablet-cursor-sync.md) 的 Follow-up。tablet 绝对光标流畅但**大幅拖拽窗口卡顿**,本批定位真凶并修复。commit `a3c4a36`。
>
> **TL;DR**:真凶不是先前 note 断言的「大区 flush(uncached framebuffer)」,而是**拖拽每帧冗余重渲整个终端**——`handle_mouse` 拖拽分支调 `draw_title_bar()+draw_content()`,而 `draw_content()->on_paint()->render_to_canvas()` 逐像素重画 80×25=2000 格;但拖拽时窗口内容不变、只是位置变,离屏 canvas 持久,`composite()` 的 `blit_to` 自动贴到新位置。删掉这两个调用,卡顿消失(用户实测「好多了」)。framebuffer 未设 PCD,QEMU 下多为 cached,flush 本不慢——先前的 flush 误判被推翻。

## 现象

tablet 绝对光标精确流畅,但**拖动窗口(尤其大终端窗)明显卡顿/掉帧**。先前 note([tablet-cursor-sync Follow-up](2026-06-24-f5-m5-tablet-cursor-sync.md))诊断「瓶颈是大区 flush(framebuffer uncached MMIO 逐字节写)」,据此尝试 flush 宽化 + 拖拽局部失效——**实测无改善**,且局部失效还踩出过残影(漏 title bar `total_height`)。

## 重新诊断:找「操作 A 有、操作 B 没有」的差异

性能定位的关键杠杆:**光标流畅 vs 拖拽卡,两者每帧都走 `composite()` 全屏(clear + blit 所有窗口 + cursor)**。既然共同路径不卡,瓶颈必在**拖拽特有**的步骤。

拖拽 `MouseMove`([window_manager_input.cpp](../../kernel/gui/window_manager_input.cpp))比光标多做了:

- `focused_->draw_title_bar(*font_)` —— 重画 title bar 离屏;
- `focused_->draw_content()` —— 清内容区 + `on_paint()`。

而 `Window::draw_content()`([window.cpp](../../kernel/gui/window.cpp))→ `on_paint(canvas)` → `Terminal::render_to_canvas()`([terminal.cpp:377](../../kernel/gui/terminal.cpp#L377))**逐像素重渲整个终端**:每格 `draw_rect`(清背景,逐像素)+ `draw_pixel`(画字形,逐像素)。80×25=2000 格 × ~128 像素 ≈ 25 万次像素操作,**每帧、每个终端、内容未变也照渲**。这才是拖拽帧的超预算开销。

## 根因

拖拽时**窗口内容不变,只是屏幕位置变**。`Window::canvas_`(离屏 title bar + content)是持久成员;`set_position()` 只改 `x_/y_`。`composite()` → `blit_to()`([window.cpp:69](../../kernel/gui/window.cpp#L69))用这份现成 canvas + 新坐标 blit 到 back buffer。所以拖拽**根本不需要重画离屏**——原代码每帧 `draw_title_bar()+draw_content()` 是纯冗余(且 `draw_content` 触发终端全量重渲)。

光标路径不碰 `draw_content`,所以流畅;拖拽碰,所以卡。差异吻合。

### 推翻「flush 是瓶颈」

先前 note 假设 framebuffer 是 uncached MMIO。但 [framebuffer.cpp:29](../../kernel/drivers/video/framebuffer.cpp#L29) 映射时 `mmio_flags = FLAG_PRESENT | FLAG_WRITABLE`,**没设 PCD/PWT**;CinuxOS 无 UEFI firmware 配 MTRR,QEMU 默认高地址物理内存为 WB(Write-Back,cached)。故 flush 全屏是 cached 写,本不慢——宽化 / 局部失效自然无感。

## 修法

**核心**([window_manager_input.cpp:85](../../kernel/gui/window_manager_input.cpp#L85)):拖拽分支删掉 `draw_title_bar()`+`draw_content()`,只留 `set_position()`+`invalidate_all()`。离屏 canvas 内容保持,blit 到新位置。

**顺带**([host_cinux.cpp:318](../../kernel/gui/host_cinux.cpp#L318)):`cinux_flush` 逐字节 `volatile uint8_t` 写改 uint32 整像素写(framebuffer 已 32 位对齐,stride/pitch 整像素倍)。`-O2` 下对 volatile 指针编译器**不能**合并 / 向量化 store(每次访问是须保留的副作用),故旧 byte 循环原样保留;改 uint32 直接 ÷4 store。fb cached 下收益小,但属正确反模式修复(真硬件 / uncached 路径下有感),零正确性风险。

## 验证

- run-kernel-test **931/0**(不回归)。
- 用户 GUI 实测拖拽:**「好多了」**。

## 残留 / 下一步(若仍不够流畅)

拖拽帧仍每帧 `composite()` 全屏:`screen_->clear()`(memfill32 3MB,可接受)+ **每窗逐像素 `blit()`**([canvas.cpp:278](../../kernel/drivers/canvas.cpp#L278))。本批未碰。若进一步优化:

1. **`blit` 行批量**:窗口在屏内时每行连续 `w` 像素,用 memcopy 行批量代替逐像素(266K 迭代 → N 行 memcopy)。
2. **composite dirty 区裁剪**:只 clear+blit dirty 区与各窗的交集(需给 clear/blit/icon/cursor 加 clip rect 参数,大重构)。
3. **终端增量渲染**:`render_to_canvas` 只重渲变化的格(当前每帧全渲,内容未变时纯浪费;可记 dirty cell 集)。

(用户表示「先这样」,暂不动。)

## GOTCHA 登记候选

- **GOTCHA(拖拽重画离屏 = 冗余)**:窗口离屏 canvas(title bar + content)持久;`set_position` 只改坐标,`composite` 的 blit 自动贴新位置。拖拽 / 移动时**无需** `draw_title_bar`/`draw_content`——后者触发 `on_paint`→`render_to_canvas` 全量重渲,是隐藏性能陷阱。重画只在内容**变化**(创建 / 输入 / 滚动)时做。
- **GOTCHA(性能定位:找差异)**:两个操作一卡一顺,先列**共同路径**(都 composite 全屏),共同路径不卡则瓶颈在**差异步骤**;别急着归因共同路径里最显眼的(flush)。
- **GOTCHA(framebuffer cache 属性)**:`map_2mb` 若只给 PRESENT|WRITABLE 不设 PCD,QEMU(无 firmware 配 MTRR)下高地址 framebuffer 默认 WB cached——不是 uncached MMIO。判定 flush 是否瓶颈前先确认 PCD / MTRR。
- **GOTCHA(volatile 阻止 store 合并)**:`-O2` 不会把 `volatile uint8_t*` 的逐字节 store 合并 / 向量化(每次访问是须保留的副作用)。对已知对齐的 MMIO / 帧缓冲,手写 uint32/uint64 宽写。

## 改动文件

- [kernel/gui/window_manager_input.cpp](../../kernel/gui/window_manager_input.cpp):拖拽删冗余 `draw_title_bar`/`draw_content`。
- [kernel/gui/host_cinux.cpp](../../kernel/gui/host_cinux.cpp):`cinux_flush` byte→uint32 宽写。
