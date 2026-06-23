# 2026-06-23 F5-M5 批4B — 鼠标输入分层收拢(XhciSlot 退回通用 + UsbMouse 类 + drivers/mouse/)【重构】

## 背景 / 动机

4A/4B 初版「梭哈」写快了,用户当面戳破两个 cohesion 坏味道(已记 [[xhci-hid-layering-antipattern]],完事后单独审查 + 记 CODING-TASTE):
1. **传输类被应用污染**:`XhciSlot`(通用 xHCI 设备槽)里塞了鼠标专属的 `mouse_ep_dci_`/`report_buf_`/`poll_mouse_report()`/`set_protocol()`。
2. **目录散落**:鼠标代码散在 `drivers/mouse.cpp`(PS/2)+ `drivers/usb/hid.hpp`(USB HID),不在一个子系统目录。

本批重构修正分层,再续 5A。

## 变更(重构 + 4B 注入)

**目录收拢** —— `drivers/mouse/` 收所有鼠标:
- `git mv mouse.cpp/mouse.hpp → mouse/{mouse.cpp,mouse.hpp}`(PS/2 + 光标 sink);6 处 include 跟改。
- `git mv usb/hid.hpp → mouse/hid.hpp`(boot-mouse 解码 + 描述符 walk);其 `usb_descriptor.hpp` include 改 repo-root-relative。
- **`drivers/usb/` 退回纯 xHCI/USB 传输栈**(xhci_controller/slot/ring/trb/registers/context/irq + usb_request/usb_descriptor)。

**XhciSlot 退回通用传输**:
- 删 `set_protocol`(HID 类请求)、`poll_mouse_report`(HID 解码)、`report_buf_` 成员。
- `mouse_ep_dci_` → `int_ep_dci_`(通用「这个 slot 配的 interrupt-IN EP 的 DCI」)。
- 加通用 **`poll_interrupt_in(hc, buf_phys, len) → ErrorOr<uint32_t bytes>`**:Normal TRB + doorbell DCI + 等 Transfer Event,返回字节数(不解码)。XhciSlot 现在只懂「USB 设备 + interrupt-IN EP」,不认识「鼠标」。

**新 `UsbMouse` 类**(`drivers/mouse/usb_mouse.{hpp,cpp}`,namespace `cinux::drivers`)—— 收拢鼠标专属:
- 持报告缓冲(DmaBuffer)+ 绑定的 `XhciSlot*`。
- `bind(slot)`;`init(hc, BootMouseEp)`(SET_PROTOCOL(boot) + add_interrupt_endpoint);`poll(hc) → HidMouseReport`(poll_interrupt_in + decode_boot_mouse)。
- 依赖单向:`mouse/` → `usb/`(传输)。

**Mouse sink(4B 注入 + 互斥)** —— `mouse.cpp`:
- 提取 `apply_motion(dx, dy_screen, buttons)`(光标更新 + 边沿 + 入队的共享尾);PS/2 `decode_packet` 传 `-dy`(反转),USB 传 `+dy`(不反转)。
- `inject_usb_motion(dx, dy, buttons)`:HID 报告→MouseEvent,**dy=+hid_dy 不反转**(GOTCHA:与 PS/2 反)。按键 bitmask `buttons & (LEFT|RIGHT|MIDDLE)`(HID/PS/2 位重合,一行,非三元链)。
- `usb_primary_` 互斥:`set_usb_primary(true)` 后 `process_byte` no-op(禁 PS/2 喂队列,保 SPSC 单生产者)。

**test**:`test_hid_mouse` 改用 `UsbMouse`(bind/init/poll),不再调 slot 的鼠标方法。

## 验证

- 全量 build 绿(reorg + 重构编译过)。
- run-kernel-test:**ALL PASSED**(回归)。
- run-kernel-test-xhci:**ALL PASSED** —— `HID mouse: iface=0 ep1-IN maxp=4 interval=7 -> boot ok`(UsbMouse.init 经通用 XhciSlot 的 set_protocol + Configure Endpoint 成功)+ `mouse idle`(静止 NAK 符合预期)。**分层重构后行为不变**。
- test_host:**54/54**。
- clang-format + 全文件 <500(mouse.cpp 428 / slot.cpp 241 / usb_mouse.cpp 68)。

## 教训(记 CODING-TASTE,完事后)

赶进度(「梭哈」)时牺牲了分层(传输类被应用污染)+ 简洁(写算恒等的三元链)。OS 这种长期项目,解耦边界(传输 vs 应用)+ 最简表达不能让步——后面解耦费劲。详见 [[xhci-hid-layering-antipattern]]。

## 遗留

- **5A**:main.cpp boot 接线(枚举 + UsbMouse.init)+ worker 轮询任务(UsbMouse.poll → Mouse::inject_usb_motion)+ CINUX_QEMU_USB option + `make run` 光标动。
- 完事后单独审查 xHCI/HID 全套 + 记 CODING-TASTE。

---

commit：（本次,批4B 重构）。
