# F-GUI 批4 GUI 收尾弧

**日期**:2026-07-07　**分支**:`worktree-gui-userspace`(本地,未 push)　**子模块 commit**:`7b2f61c`(Cinux-GUI core)　**前置**:keyboard MSI-X EHB 修(`bd90833`)

keyboard 修好后的 GUI 收尾弧。用户态 host(`/cinux_gui_host`)的 terminal / 窗口 / 配色 / 交互一轮修齐。

## 改动

**core(Cinux-GUI `7b2f61c`,7 文件)**:
- `terminal`: `prev_cursor_row_`(光标移动后旧位置行 dirty,擦 trail);CSI J mode 0/1/2 精确到 **cursor 列**(修 clear/backspace 误擦 prompt);DEL(`0x7f`)当退格(busybox line-edit 发 0x7f 非 0x08);blue `palette[4]` 0x80→0xFF(深底清晰)
- `window`: `collect_dirty/clear_dirty` override 显式递归 `content_`(content 是 child,但基类递归没到 → scroll `dirty_all_` 不传 WM —— 显式补)
- `window_manager`: 删 `bg_` 直读 `theme_->background`(单一源,原 WM `bg_=0x202020` 盖了 theme)+ `on_remove` 回调(host close term_win 时清 PTY + reset `sh_master_fd`,reopen)
- `theme`: dark `background` 0x2A1C12 深咖啡(was 纯黑;用户要暖色、杜绝蓝紫 AI 配色)

**worktree(host + cmake)**:
- `main.cpp`: `on_win_removed`(reset `sh_master_fd`,reopen)+ 窗口高加 `kTitleBarHeight`(cursor 行不被裁)
- `cmake/qemu.cmake`: host `add_custom_command`(DEPENDS core 22 cpp,**改 core 自动重编 host**)+ `create_ext2` DEPENDs host + `update-rootfs-host` target(debugfs 原地换 host 进 rootfs-gcc.ext2,不重组 buildroot)+ `run` 依赖它
- `scripts/update_rootfs_host.sh`: debugfs 换 `/cinux_gui_host`

## ⭐ 关键调试教训

1. **host 不在 cmake**:`cmake --build build` 只编 kernel,host ELF 走 `tools/musl/build-cinux-gui-host.sh`(独立 musl 交叉编译)。改 core 后 cmake build **不重编 host + 不更新 rootfs**,跑旧 host(我踩了好几轮才觉察)。修:host custom_command DEPENDS core sources。
2. **drain byte hex 仪器**:terminal 行为不对时,host drain 后 log shell 字节 hex,直接看 shell 发啥 —— 锁定 backspace = `\b ESC[J`(CSI J mode 0)。
3. **CSI J 精确 cursor 列**:ANSI J mode 0 = cursor 到屏尾(**cursor 列右** + 下方整行),不是整行。line-edit `\b ESC[J` 删输入字符不擦 prompt(cursor 左)。
4. **Window content dirty 传播**:content 是 child(set_content add_child),但 Widget 基类 collect_dirty 递归没到 content(scroll dirty_all_ 断链)—— Window override 显式递归 content_ 补。同族 WM 递归 windows_。
5. **配色单一源**:WM 原持 `bg_=0x202020`,盖了 theme.background。统一到 theme 一处(改 theme.background 生效)。

## 残留(open,交接下个 AI)

- **mm PageCache corrupt**(memory `f-gui-b4-shell-spawn-handoff`):nested-fork + dynamic ELF 触发。**新证据**:GUI shell + ls + 多次 fork/execve 后,突发大量随机 scan(`ascii=0 scan=0xd4/0xa9/0xff` 非 scancode)= USB HID `report_buf` 被 silent corrupt(mm 坏物理页)。buddy DIAG 没 silent 抓到。下个 AI 用 5 轨迹法(`mm-mapcount-munmap-cache-phys`)啃。**这不是 GUI bug**(改动全在 host 用户态,碰不到 kernel input 内存)。
- **worktree kernel dirty(未 commit)**:`page_fault/vmm/pmm.hpp`(mm SMP race 修复,验证过)+ `buddy.cpp`(DIAG,留 PageCache corrupt)+ `devfs pts` + `pty_device` + `sys_open/sys_ioctl`(shell spawn handoff)。这些是批4 shell spawn + mm,**不是 GUI 收尾弧**,留 dirty 交接(下轮 mm 修完一起 commit)。
- **滚轮翻动**:PointerPayload 无 wheel + terminal 无 scrollback。新 feature(wheel 事件 + scrollback buffer),follow-up。
- **e1000 poll 改中断**:memory 老注释"QEMU 不 latch MSI"(像 xHCI EHB 甩锅,可能是 CinuxOS bug)。virtio-net 已 MSI-X 真中断(批5),接栈 follow-up 比 e1000 改中断前瞻。

## 验证

GUI run(用户验):keyboard 打字 + mouse click 关窗 + shell 目录蓝色 + `clear` 清屏 + scroll 滚屏 + backspace 删字符(prompt 留)+ reopen shell + 深咖啡桌面。console gate 未跑(改 core + host,没动 kernel 公共接口;push 前补 `cmake --build build -j$(nproc)` 全量)。

## 关键文件

- 子模块 `7b2f61c`: `core/{theme, widget/terminal, widget/window, widget/window_manager}.{hpp,cpp}`
- worktree: `user/cinux_gui_host/main.cpp` + `cmake/qemu.cmake` + `scripts/update_rootfs_host.sh` + 子模块 pin
