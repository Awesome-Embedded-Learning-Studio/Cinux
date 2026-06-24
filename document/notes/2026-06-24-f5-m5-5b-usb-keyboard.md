# F5-M5 批5B — HID boot keyboard 生产闭环（复用 async + TransferListener）

> 日期 2026-06-24 / 批 5B / 分支 feat/f5-m5-xhci-3 / commit 5238e10

## 背景

批5A 把 USB mouse 用 async interrupt-IN + TransferListener 分发接通 GUI，并建立了通用机制（controller 按 slot 分发 Transfer Event、不解释 payload）。批5B 复用同一机制接通 **USB boot keyboard**——与 mouse 完全对称：同款 slot/EP/arm/`on_transfer_complete` 路径，只新增 keyboard 专属的 HID 解码 + 注入。达成里程碑目标「USB HID boot 鼠标+键盘」。

## 设计：复用机制 + keyboard 专属解码

**HID 层**（[keyboard/hid.hpp](../../kernel/drivers/keyboard/hid.hpp)，对称 mouse/hid.hpp）：
- `HidKbdMod` modifier 位（LCtrl/LShift/...）+ `HidKeyboardReport`(modifier + reserved + 6 keycodes)
- `decode_boot_keyboard`（纯）+ **HID Usage ID → ASCII 表** `kHidUnshifted`/`kHidShifted`（59 项，USB HID Usage Tables v1.21 US 布局；独立于 PS/2 的 scancode set 1 表）
- `find_boot_keyboard`（仿 `find_boot_mouse`，protocol `kBootProtoKeyboard`=0x01）+ `BootKeyboardEp`
- 通用 `UsbXfer`（transfer type）从 mouse/hid.hpp **提到 usb_descriptor.hpp**——让 keyboard/hid.hpp 不跨 include mouse/，两个输入子系统只共依赖 usb/

**Keyboard 注入**（[keyboard.hpp/cpp](../../kernel/drivers/keyboard/keyboard.cpp)）：
- `inject_usb_report(modifier, keycodes, n)`（hard-IRQ，对称 PS/2 `irq1_handler`）：modifier→shift/ctrl/alt；**对比 `usb_prev_keys_[6]` 边沿检测** press（新增 keycode）/release（消失 keycode）；HID keycode→ASCII（shift 选表）；`dispatch_key` 入队 + GUI KeyDown/KeyUp
- `dispatch_key` **提取自 irq1_handler 共享尾**（PS/2 + USB 都走它，DRY）
- `set_usb_primary` + `usb_primary_`：USB primary 时 `irq1_handler` 读 sc 后早退（不喂队列，保 SPSC 单生产者）

**UsbKeyboard**（NEW [usb_keyboard.hpp/cpp](../../kernel/drivers/keyboard/usb_keyboard.cpp)，仿 UsbMouse）：`TransferListener`——`init`(SET_PROTOCOL boot + Configure Endpoint) / `on_transfer_complete`(decode → `Keyboard::inject_usb_report` → `arm`) / `arm`(async submit)。

**boot 枚举**（[usb_init.cpp](../../kernel/drivers/usb/usb_init.cpp) 重构）：`g_slots[2]`（每设备一 slot）+ `g_mouse` + `g_keyboard`，扫端口 → `enumerate_port`（提取复用：reset/Enable Slot/Address/GET_DESCRIPTOR）→ `find_boot_mouse`/`find_boot_keyboard` 判断 → 对应 driver `init` + `register_transfer_listener` + `arm` + `set_usb_primary`。`slot_idx` 每接通一设备 +1。

## 关键点

1. **边沿检测（USB keyboard 报告语义）**：boot keyboard 报告是「当前按下键集合」，非 PS/2 的 make/break 单键 → 必须对比上份 report 集合判 press/release（`usb_prev_keys_`）。
2. **HID keycode ≠ PS/2 scancode**：USB 用 HID Usage ID（page 0x07），需独立 ASCII 表（4=a/30=1/...），不能复用 keyboard.cpp 的 `kScToLower`（scancode set 1）。`KeyEvent.scancode` 字段 USB 填 HID usage ID。
3. **双设备枚举 slot 管理**：每设备独立 slot（不同 slot_id），`g_slots[2]` 按发现顺序分配；`enumerate_port` 提取让 mouse/keyboard 分支共享地址+描述符逻辑。
4. **dispatch_key 提取**：PS/2 irq1 和 USB inject 共享「构建 KeyEvent + enqueue + GUI dispatch」，提取后 irq1 变短且 DRY。

## 验证

| 目标 | 结果 |
|------|------|
| run-kernel-test（基线） | 931/0（test_keyboard 不回归：dispatch_key 提取行为等价） |
| run-kernel-test-xhci | 931/0 ALL PASSED |
| make run（big_kernel，async mouse+keyboard） | `[xHCI] HID boot keyboard armed port=4 ep1-IN` + `mouse armed port=5 ep1-IN` + GUI 桌面无 panic |
| test_host + 全量编译 | 绿（1918 symbols，+9） |

键盘实际输入需手动（`make run` 后敲键看终端收字符，留用户验证）。

## 收官

**F5-M5 里程碑核心达成**：xHCI 主控 → MSI-X 中断 → async interrupt-IN → HID boot mouse + keyboard → 现有 `Mouse::event_queue()` / `Keyboard` 队列 → GUI。PS/2 作静默 fallback（USB primary 时禁喂队列，代码保留）。批5C 收尾 docs（ROADMAP/PLAN/todo）。
