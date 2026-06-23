# 2026-06-23 F5-M5 批0C — xHCI 中断向量 + ISR stub + handler + 注册

## 背景 / 目标

0A/0B 落 MSI-X 发现 + 编程。0C 把 xHCI 事件环中断接到 IDT:固定向量 `0x40`、asm stub（interrupts.S）、C handler（计数 + hook + EOI）、irq_init 注册。handler 留 hook,2C 由控制器填事件环服务（清 IMAN.IP / 推进 ERDP）。"向量真触发"证明留 2C。**Phase 0（MSI-X 可复用子系统）至此交付完毕**,只剩 2C 端到端触发证明。

## 变更

- xhci_irq.hpp（NEW）:`kXhciIrqVector=0x40`、`XhciIrqHook`、`set_xhci_irq_hook`、`extern g_xhci_irq_count`。
- xhci_irq.cpp（NEW,kernel-only）:handler（计数→hook→`irq_eoi(0)`）+ `set_xhci_irq_hook`（file-static g_xhci_hook）。
- interrupts.S:+ `ISR_NOERRCODE xhci_irq_stub, xhci_irq_handler`（仿 reschedule_ipi_stub）。
- irq_handlers.cpp:+ include xhci_irq.hpp、+ `xhci_irq_stub` decl、+ `#ifndef CINUX_USB` 默认 handler（USB-off 保链接,仿 mouse）、+ irq_init 注册 0x40。
- kernel/CMakeLists.txt:+ `target_compile_definitions(big_kernel_common PUBLIC CINUX_USB)`（option→-D,使 #ifdef 与文件 gate 一致）。
- drivers/CMakeLists.txt:+ usb/xhci_irq.cpp。

## 关键陷阱（GOTCHA）

- **CINUX_USB option 必须转编译定义**:`option()` 只是 CMake 变量（控文件 gate）,不是 -D 宏。irq_handlers.cpp 的 `#ifndef CINUX_USB` 看不到它 → 编了默认 handler,与 xhci_irq.cpp 撞（**multiple definition** 链接错误）。修:kernel/CMakeLists.txt 加 `target_compile_definitions`（仿 CINUX_GUI 在 :107）。**这是 option→宏的标准转法,后续 CINUX_USB 代码 #ifdef 都依赖它**。
- **向量在 irq_init 注册（非驱动 init 时）**:共享 IDT,启动期注册避免 AP 起来后的 IDT 竞态（recon 建议）。0x40 在 2C 编程 MSI-X 前不会触发,handler 是 no-op+计数,无害。
- **USB-off 保链接**:asm stub 总编（interrupts.S）→ 总引用 `xhci_irq_handler` 符号。USB off 时 usb/xhci_irq.cpp 不编,故 irq_handlers.cpp `#ifndef CINUX_USB` 提供默认 EOI handler 保链接（仿 mouse `#ifndef CINUX_GUI`）。

## 验证

- kernel:全量构建绿（CINUX_USB 编译定义修正后,无 multiple definition）。
- run-kernel-test:**928/0** —— 启动不三重错误,证明 asm stub + IDT 注册正确（向量未触发,但注册非致命）。
- 诚实标注:handler 从未触发（无 MSI-X 触发源）,计数器 + hook 端到端留 2C doorbell-NOOP 测试。

## 遗留

- 1A:PCI `find_xhci`（class 0x0C/sub 0x03/prog_if 0x30）。
- 2C:`set_xhci_irq_hook` 注册控制器事件环服务,doorbell NOOP→Command Completion Event 触发 0x40,断言 `g_xhci_irq_count` 上升。

---

commit：（本次,批0C）。
