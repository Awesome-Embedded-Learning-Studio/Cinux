# 2026-06-23 F5-M5 批1C — xHCI 控制器 init(PCI 使能 + BAR0 映射 + halt/reset)

## 背景 / 目标

**首块真硬件交互。** XHCIController（仿 AHCI instance+singleton）:使能 PCI BusMaster+MemSpace、映 BAR0（KMEM_MMIO+0x20000,4 页 PCD）、读 cap（CAPLENGTH/MaxSlots/MaxPorts/DBOFF/RTSOFF）、halt（USBCMD=0→等 HCH）+ reset（HCRST→等 CNR 清）。BIOS handoff 跳过（QEMU qemu-xhci 启动即 OS-owned;真机留 follow-up）。

## 变更

- xhci_controller.{hpp,cpp}（NEW）:XHCIController 类 + init（返回 ErrorOr<void>,reset 超时→TimedOut）。MMIO @+0x20000,4 页。
- kernel/test/test_xhci.cpp（NEW）:find_xhci（找不到则跳过=过）+ init + 断言 reset（HCH set/CNR clear/MaxPorts>0）。`run_xhci_tests()`。
- main_test.cpp:+ run_xhci_tests 声明 + 调用（AHCI 之后,VMM 已 up）。
- cmake/qemu.cmake:+ `run-kernel-test-xhci` target（同 run-kernel-test + `-device qemu-xhci,id=xhci -device usb-kbd/usb-mouse,bus=xhci.0`;**不动** QEMU_COMMON_FLAGS,基线零扰）。
- kernel/CMakeLists.txt:+ test/test_xhci.cpp 进 big_kernel_test。
- drivers/CMakeLists.txt:+ usb/xhci_controller.cpp。

## 关键陷阱（GOTCHA）

- **usb 与 pci 是兄弟命名空间**:xhci_controller.cpp（`usb::`）里 bare `PCI`/`PciReg`/`PciCmd` 查不到（pci 是兄弟非父）,必须 `pci::` 限定。msix_controller（`pci::msix`,pci 的子）bare 能查到 —— 别混。
- **bare `#include "pci.hpp"` 跨目录失败**:xhci_controller.cpp 在 usb/,pci.hpp 在 pci/,须全路径 `kernel/drivers/pci/pci.hpp`。
- **using-declaration 不能命名 namespace**（复现 0A GOTCHA）:test_xhci.cpp 的 `using ...::Usbsts` 失败,改 `using namespace usb`。

## 验证（★ 首个真硬件端到端证明）

- **run-kernel-test-xhci**:**929/0** —— `[PCI] xHCI found: 00:05.0 BAR0=0xfebf0000` + `CAPLENGTH=64 MaxSlots=64 MaxPorts=8 DBOFF=0x2000 RTSOFF=0x1000` + `controller reset complete (halted, CNR clear)` + `reset test passed: MaxPorts=8 USBSTS=0x1`（USBSTS=0x1=HCH）。**xHCI 控制器在 QEMU 里点亮!**
- run-kernel-test（默认,无 xHCI）:**929/0**（test_xhci 跳过=过）。基线 928→929（+1 跳过测试）。

## 遗留

- 2A:TRB + ring 数学（纯）。
- 2B:DCBAA + 中断器/ERST + USBCMD.RS 启动。
- 2C:接线 MSI-X→event-ring ISR + doorbell NOOP→收中断。
- BIOS/SMM Legacy Support handoff 留真机 follow-up（QEMU 不需要）。

---

commit：（本次,批1C）。
