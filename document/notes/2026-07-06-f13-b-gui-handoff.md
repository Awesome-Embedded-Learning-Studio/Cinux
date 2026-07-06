# F13-B GUI host adapter — 阶段交接（WIP，2026-07-06）

> 分支 `worktree-f13-gui`（worktree `.claude/worktrees/f13-gui`，从 main `bef86b0`）。
> F13-A（Cinux-GUI 主体 P0-P7）✅；F13-B（host adapter 重写）批1a/1b 已做（`dd0e18f`），批2（PTY 真终端）待续。
> 本次会话定位了 2 个独立 bug（一个修了，一个 workaround），登记留给下一个 AI 在本分支继续。

## Bug ① 脏区行绘制极端偶尔画花（严重残影）⏳ 未修

**报告**：用户 2026-07-06 报告。脏区行绘制（per-rect dirty flush）极端偶尔会 bug，画面画花，严重残影。

**状态**：未诊断。怀疑方向：
- `Compositor::render` 末尾 cursor 用 `cur()`（dirty rect）clip，per-rect 渲染时 cursor 只画在含 cursor 的 dirty rect —— cursor 移动 footprint（老+新）若 dirty rect 覆盖不全，残留
- 或 flush 路径（`host_cinux::cinux_flush`）逐 rect 拷贝到 framebuffer，rect 重叠/漏拷
- 或 `paint_list_` cmd 在 per-rect 渲染被重复执行（每 dirty rect 都 composite 全 paint_list）

**下一步**：复现（疯狂拖拽 + 频繁 dirty）→ 抓画面 → 看是 staging 错还是 flush 错。

## Bug ② PIT tick 踩 gui_worker render 栈（#GP/#UD）⏳ workaround

**症状**：疯狂拖拽 / USB tablet 自动报告触发 render 时，崩在 `fill_rect`/`glyph_blit`/`effective_bounds`（崩点漂移），异常 #GP/#UD 漂移。单核也崩（不是 SMP）。SMP（-smp 2）开头也崩（同族 IRQ 路由问题）。

**诊断过程**（本次会话，逐步排除）：
- BSS 全好（`check_intact` 检查 `font.glyphs_` / `paint_list.count` / `host.ctx`，零报告）
- font 引用变栈地址（draw_text 收到栈上伪 `glyphs_` = `0xF000FF5400E99CCC` 固定值）
- 各层入口 trap（`Compositor::render`/`h_text` font 检查、`fill_rect` s 字段检查）都不触发（入口合法）
- 但内部 row/font 局部被踩 → 写非 canonical → #GP
- 栈 8KB 铁律（`task_builder.hpp` static_assert），崩时 RSP 用 ~3KB（没溢出）
- **决定性实验**：`InterruptGuard` 包 `desktop.render`（禁 PIT tick）→ **不崩**
- **结论**：PIT tick（100Hz IRQ0）在 gui_worker render 中段抢进，handler 压栈 + `Scheduler::tick` 踩了 render 帧

**这不是 GUI bug**，是 timer/scheduler 框架问题（跟 [f-usability-symlink-follow] PIT IRQ inline schedule 重入、[smp-shell-migration-df-open] 同族）。

**workaround**（当前 commit）：`host_cinux::cinux_render_frame` 用 `InterruptGuard` 包 `desktop.render`。副作用：USB/PS2 ISR 延迟（鼠标光标卡顿）。

**正解**（独立弧）：修 PIT/scheduler 框架。方向：
- `Scheduler::tick` 内部某写越界踩 gui_worker 栈？
- tick handler 压栈太深 + render 栈深？
- 参考 memory `sys-ping-df-sti-in-syscall` / `f-usability-symlink-follow` 同族修复

## Bug ③ cursor 不显示（F13-B 迁移遗留）✅ 已修

**根因**：`host_cinux::cinux_dispatch_event` 调 `st->desktop.dispatch_pointer(p)`，但 `Desktop::dispatch_pointer` 不更新 cursor（注释明说 "Move with no press_target is ignored -- P3 has no hover"）。cursor 更新在 `WindowManager::process_pointer`（`cursor_x_/y_` + invalidate footprint）。迁移时（`dd0e18f`）改错了。

**修法**：改回调 `st->wm.process_pointer(p)`（跟 fbdev host `host/linux_fbdev_main.cpp:93` 一致）。

## 当前 worktree 状态（WIP commit）

**真修复**（留）：
- `host_cinux.cpp` cursor fix（`wm.process_pointer`）
- `vkprintf_impl.hpp` `%zu` 支持（`%zd/%zu/%zo/%zx/%zX`，LP64 等价 `long`）—— 顺带改的，host kprintf 测试 2/2 PASS

**用户改动**（留，批1a 一部分）：
- `widget.hpp/cpp` `PaintList paint_list_` 改 Desktop 成员（移出栈，避 128KB `PaintList` 在 8KB kernel 栈必爆）

**诊断代码**（下一个 AI 决定撤/留）：
- `host_cinux.cpp`：`check_intact`（font.glyphs_ + paint_list.count + host.ctx，5 打点 dispatch:entry/exit + render:entry/after-desktop-render/exit）+ `render#N` dump（前 10 帧 staging/g_state/font）+ Surface s 每帧检查 + `InterruptGuard` 包 desktop.render（Bug ② workaround）
- `swraster.cpp` `fill_rect` / `compositor.cpp` `h_text`+`Compositor::render`：`__builtin_trap` 入口检查（s 字段 / font 引用合法性）
- `font.hpp` `debug_glyphs_ptr()` getter / `widget.hpp` `debug_paint_count()` getter

**未提交**：`log.txt`/`log2.txt`（临时日志，删）

## 下一步（下一个 AI 在本分支续）

1. 修 Bug ②（PIT/scheduler 框架）→ 撤 `InterruptGuard` workaround
2. 修 Bug ①（脏区残影）—— 复现 + 抓画面
3. 撤诊断 trap（`fill_rect`/`h_text`/`Compositor::render` 入口 trap、`check_intact`、`render#N` dump、`Surface s` 检查），保留 cursor fix + `%zu` + `paint_list_` 成员
4. F13-B 批2（PTY 真终端：`TerminalSession` + `HostDesktop::spawn` fork+execve /bin/sh + PTY slave，复用 F10-M3 PTY 框架）
5. 收官：note + ROADMAP F13-B ✅
