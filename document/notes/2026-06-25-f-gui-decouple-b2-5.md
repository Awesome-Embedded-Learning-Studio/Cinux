# F-GUI-DECOUPLE 批2-5 — init/main/irq 源码 #ifdef 全消 + USB HID gate（§14 收官）

> 里程碑 F-GUI-DECOUPLE（GUI 模块独立化）批2-5。分支 `feat/gui-decouple`（rebase main 含 SMP 修复 PR#34）。
> 批1 见 [2026-06-25-f-gui-decouple-b1-usb-stub.md](2026-06-25-f-gui-decouple-b1-usb-stub.md)。SMP 迁移竞态修复见 [2026-06-25-f4-followup-smp-migration-race.md](2026-06-25-f4-followup-smp-migration-race.md)（PR#34）。
> commits：批2 `7e32888`（launch_userspace）+ 批3 `f39cbe8`（handoff）+ 批4 `eaaca9c`（irq stub）+ 批5 `d4012c6`（USB HID gate）。

## 批2：launch_userspace() 抽象消 init.cpp 头号反例

`init.cpp` `kernel_init_thread` 读到一半 `#ifdef` 分叉两路（GUI 启桌面 / 非 GUI fork shell）—— **§14 头号反例原型**。抽单一接口 `launch_userspace()`，两实现 CMake 选编：

- `kernel/gui/desktop_launch.cpp`（GUI）：`gui_start` + 起 `gui_worker`（含 `usb::poll_input`）
- `kernel/proc/shell_launch.cpp`（非 GUI）：`fork` + `execve /bin/sh`
- `kernel/proc/userspace.hpp` 无条件声明接口

`init.cpp` 一句 `launch_userspace()`，零 `#ifdef`（126→63 行）。**触发 SMP bug**：跨 TU 抽函数时序让 gui_worker 落进迁移窗口，把偶发 panic 改必现 → F4-followup（PR#34 `task on_cpu`）修复。

## 批3：handoff_framebuffer_to_gui() 消 main.cpp #ifdef

`main.cpp` Step 15b（Canvas + gui_init + console detach）`#ifdef` 块 → 抽 `handoff_framebuffer_to_gui(fb, font, console)`：GUI 实现（desktop_launch.cpp）+ 非 GUI stub（shell_launch.cpp）。`main.cpp` 一句调用，include 块删，零 `#ifdef CINUX_GUI`。

## 批4：irq_handlers #ifndef stub 文件化

`irq_handlers.cpp` 的 `mouse_irq12_handler`（`#ifndef CINUX_GUI`）+ `xhci_irq_handler`（`#ifndef CINUX_USB`）stub 挪独立文件（mouse_stub.cpp / usb_stub.cpp）+ CMake else 选编。`irq_handlers.cpp` 零 `#ifndef`。

## 批5：USB 拆核心传输/HID 双 gate（非 GUI+USB 断链修复）

批3 暴露：USB HID（`usb_init`/`usb_mouse`/`usb_tablet`/`usb_keyboard`）依赖 `Mouse`/`Keyboard`（GUI gate），非 GUI+USB on 时 HID 编但 Mouse 不编 → 断链。用户要求「不带问题上 PR」，拆 USB CMake：

- **核心传输**（xHCI controller/ring/slot/irq + MSI-X）：`if(CINUX_USB)`。用 `TransferListener`**基类**（xhci_controller.hpp），不依赖 Mouse/Keyboard，独立于 GUI 构建。
- **HID 注入**（usb_init + usb_mouse/tablet/keyboard）：`if(CINUX_USB AND CINUX_GUI)`。
- **usb_stub**（usb::init/poll_input 空）：`if(NOT (CINUX_USB AND CINUX_GUI))`。
- **usb_xhci_stub**（xhci_irq_handler 空）：`if(NOT CINUX_USB)`（从 usb_stub 拆出——xhci 真实现只 USB 时编，避符号冲突）。

非 GUI+USB 时 USB 核心编但 `usb::init` 空（xHCI 不 bring up，无害死代码）。

## 验证矩阵（4 种构建组合全绿）

- 默认（USB+GUI）：`run-kernel-test` **931/0** + `make run -smp 2` **panic 0**（AP1 online + GUI init + Desktop + xHCI USB HID 真 usb::init：tablet/keyboard armed）
- **非 GUI+USB big_kernel 构建绿**（原断链，批5 修；USB 核心 + usb_stub，无 HID）
- 非 GUI 非 USB big_kernel 构建绿（handoff stub + usb_stub + usb_xhci_stub + mouse_stub）

## 预存 follow-up（非本里程碑）

- **非 USB `big_kernel_test`** 因 `test_xhci.cpp` 无 `#ifdef CINUX_USB` 守卫链接失败（test gate，用户拍板 test 留）。
- ~~非 GUI+USB big_kernel 断链~~ ✅ 批5 修。

## §14 成果

`main.cpp` / `init.cpp` / `irq_handlers.cpp` 三处源码读半截路 `#ifdef` 全消 + USB 子系统 §14 双 gate（核心传输 / HID 注入）。开关全归 CMake，读代码不再撞 `#else` 脑补两路。
