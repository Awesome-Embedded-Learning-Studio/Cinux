# F13-B 桌面图标迁移:DesktopIcon widget + 琥珀配色

> 2026-07-06,分支 `worktree-f13-gui`(worktree `.claude/worktrees/f13-gui`)。
> 接 [`2026-07-06-f13-b-dirty-flush-fix.md`](2026-07-06-f13-b-dirty-flush-fix.md)(花屏根治)。
> F13-B 切新 core 后桌面只有 Hello 窗,无图标(老 visor 有 Shell/Calc)。本批复刻图标系统 + 换琥珀配色。

## 背景

`dd0e18f` 切新 Cinux-GUI core 时删了老 `kernel/gui/{desktop_icon,icon,data}`(~1500 行),新桌面 = `WindowManager + Window + Label`,无图标。本批把老桌面图标系统在新 core 复刻(`DesktopIcon` widget + `icon_data`),配色换琥珀暖橙。

## 配色决策(用户拍板)

避开紫蓝(经典 AI 配色)+ 区别老 dark-teal + 绿桌面。选 **琥珀暖橙**:
- **Shell**:深棕 body(`#2A1F0A`)+ 琥珀 cursor(`#FFB000`)+ 暖白 prompt + 红/黄/绿 traffic-light 点(保持)
- **Calc**:暖米 body(`#C8A878`)+ 深棕边 + 琥珀 LCD(`#FFD878`)+ 琥珀 equals 键
- **形状**:沿用老 desktop icon 的 32 行 hex nibble rows(终端/计算器形状不动),只 palette 换色 —— 还原度高、工作量最小

## 实现

| 文件 | 内容 |
|---|---|
| `core/icon_data.hpp`(新) | 搬老 `detail`(build_icon/build_mask/IconBitmap/IconMask)+ 老 `k_shell_rows`/`k_calc_rows`(形状不变)+ 琥珀 palette |
| `core/widget/desktop_icon.{hpp,cpp}`(新) | `DesktopIcon` widget:`set_bitmap/label/on_activate`;`paint_to_list` 逐像素 `fill_rect`(mask 非透明)+ label;`on_pointer` down+up in rect → `on_activate` |
| `core/widget/window_manager.{hpp,cpp}` | `icons_` 数组(对称 `windows_`):`add_icon`;paint `bg→icons→windows`;`collect/clear_dirty` 递归 `icons_`;`process_pointer` 加 icon click(window hit-test 优先,未中则 icon hit-test,press capture `icon_target_`) |
| `kernel/gui/host_cinux.cpp` | `HostState` 加 `shell_icon`/`calc_icon`;`icon_activate_cb` stub(kprintf);`cinux_host_init` 注册 icon((40,40)/(40,120),跟老桌面布局一致) |

## 双击 stub

`on_activate` = `icon_activate_cb`(kprintf)。PTY 真终端(批2 / 用户态化弧)接 Shell 双击 → `fork+execve /bin/sh`。**host 故意保持薄**(stub 无重逻辑),未来迁用户态不疼 —— 这是架构预留(见下"方向")。

## ⭐ GOTCHA

1. **`DesktopIcon::on_pointer` 要 public**。Widget 基类 `on_pointer` 是 public virtual;override 别改访问级别。我一开始放 protected,WM 持 `DesktopIcon*` 调 `on_pointer` → access error。改 public(跟 `Window::on_pointer` 一致)。
2. **`desktop_icon.hpp` include `"../widget.hpp"`**。Widget 基类在 `core/widget.hpp`(不是 `core/widget/widget.hpp`)。label.hpp 也是 `"../widget.hpp"`。我写 `"widget.hpp"` → `'widget.hpp' file not found`。跟同目录其他 widget 的 include 风格对齐。
3. **PaintList 无 blit 命令**。`DesktopIcon::paint_to_list` 逐像素 `fill_rect`(只画 mask 非透明),跟 Compositor cursor 同风格。32×32 icon ~600 非透明像素 × 2 = ~1200 cmd,`PaintList kMaxCmds=4096` 够;icon 静态不常重画,可接受。加 blit 命令是 follow-up。
4. **WM `icons_` 对称 `windows_`**。paint / collect_dirty / **clear_dirty** 都要递归 `icons_`(否则 dirty 累积 —— 同花屏 bug A 的教训)。`process_pointer`:window hit-test 优先(window 在 icon 上),未中则 icon hit-test;press capture `icon_target_`(down 的 icon 持续收 move/up)。
5. **子模块先 upstream + push,主仓 bump pin**。Cinux-GUI main:`7a159c4 → 7fb27ce`(花屏)`→ 0b0c135`(icon),已 push origin。worktree 子模块 checkout `0b0c135`,主仓 bump pin。

## 验证

- 子模块 standalone ctest **22/22 全绿**(含 `window-manager-test`,验证 `icons_` 集成改没破现有 widget 逻辑)
- worktree 编译过(core + host_cinux + kernel 镜像)
- GUI 用户自启看效果(琥珀图标 + 双击 stub 串口日志)

## 架构方向(用户决策,记这里备忘)

聊到 GUI 该在哪一层。结论:
- **Cinux-GUI core 已经 host-neutral**,子模块 `host/` 有完整的用户态 Linux host(linux_fbdev_main + evdev + posix_spawn)。**core 一行不用改**就能在用户态跑。
- **当前 GUI 跑内核态**(`gui_worker` 是 `TaskBuilder` 内核线程)是 visor 时代遗留;spawn `/bin/sh`(批2)在内核态别扭(内核线程 fork 用户态 shell 边界拧)。
- **下一弧:GUI 用户态化**(自己的桌面进用户进程,kernel `gui_worker` 退役)。DesktopIcon 等 widget 类已在 core,迁用户态只换 host adapter。host 保持薄(stub)就是为此铺路。
- **X11/Wayland 是远期北极星**(要 DRM 驱动 + syscall 扩 + evdev + 交叉编译整个发行版用户态),**不是用户态化的等号**。先做自己的桌面生态。

详见会话讨论 + 后续 F-arc 立项。

## follow-up

- **PTY 真终端 / GUI 用户态化**:Shell icon 接 `fork+execve /bin/sh`(优先走用户态化弧,spawn 自然)。
- **PaintList 加 blit 命令**:DesktopIcon 逐像素 fill_rect 是占位,blit 更高效。
- **图标双击 vs 单击**:当前单击(down+up)即触发,老桌面是双击。若要双击,加 down 间隔判断(参考老 WM pending action)。
