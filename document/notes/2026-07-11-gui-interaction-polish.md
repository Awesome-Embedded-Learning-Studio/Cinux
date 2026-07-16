# GUI 桌面交互打磨(键盘 autorepeat + 多终端 + click-to-focus)

> 日期：2026-07-11 · commits `fcf1691` + `ac68c98` + `cdf1458` · v1.0.0 发版后真实可用化 · 状态：✅ 合 main

## 背景

README / CHANGELOG 声称「多终端窗口」，实现却是单 slot；键盘长按只触发一次。发版后录屏前把桌面交互
打磨到位。三个独立 feature commit。

## 键盘 USB HID 软件自动重复（commit fcf1691）

长按键（退格 / 方向键）之前只触发一次：HID 每 ~10ms 周期上报同一 key，`inject_usb_report` 的 `key_in`
去重判「已在 prev」跳过 → 只首次 dispatch。

- `keyboard.hpp` 加 repeat 状态（`usb_repeat_key_` + `usb_repeat_deadline_`）+ 常量（**500ms 初始延迟 /
  50ms 间隔 = 20 次/秒**，Linux 手感）。
- `inject_usb_report` 改 press 循环：新 press 设 deadline；持续 key 到 deadline → dispatch repeat + 重置
  deadline；release 清 `repeat_key_`。
- 时间源 `g_hpet.monotonic_ns()`（HPET 不可用时退化为单按，安全）。

效果：GUI 长按退格连续删字符。

## 多终端（commit ac68c98）

之前单 slot（`sh_master_fd` / `term_win` / `term` 各一），`shell_activate` 第二次 click 被
「already open」return 挡 → 点不开第二个。

- `HostState` 加 `ShellSession` 嵌套 struct + `shells_[kMaxShells=4]` 数组（每个 `{Window, TerminalWidget,
  master_fd}`，-1=空闲）。
- `shell_activate`：找空闲 slot spawn + 窗口错开（24px stagger）+ 设 `focus_slot`。
- `host_render_frame`：遍历所有 active slot 各 drain。
- `host_dispatch_event`：key → `focus_slot`（最新 spawn 的；click-to-focus 当时留续）。
- `on_win_removed`：关对应 slot + focus 回退到其他 active shell。

## click-to-focus（commit cdf1458）

窗口内点击切键盘焦点到对应 shell（补多终端留续的 focus 切换）。

## 验证

两 leg run-kernel-test-all 1946/0（编译 + 无回归；repeat / 多终端手感待 GUI 验）；GUI 桌面点 Shell 图标
开多终端、窗口错开、点击切焦点、长按连续删字符。

## 教训

- README / CHANGELOG 的功能声明要和实现对齐——声称「多终端」却单 slot 是发版门面没兜住的债。
- HID 去重逻辑（防同一 report 重复 dispatch）会和「软件 autorepeat」冲突，需要显式 repeat 状态机绕开。
