# 2026-06-23 F5-M5 批1B — xHCI 寄存器布局头

## 背景 / 目标

纯类型头,定义 xHCI 三块 MMIO 寄存器（cap / operational / runtime interrupter）+ doorbell/port 偏移 + 位常量。static_assert 锁结构大小。未被 kernel .cpp 引用前不进 kernel build;host test 验 static_assert + scratchpad 解码。

## 变更

- xhci_registers.hpp(NEW):`XhciCapRegs`(0x20)/`XhciOpRegs`(0x3C)/`XhciInterrupterRegs`(0x20) packed volatile 结构 + 位常量（USBCMD/USBSTS/CRCR/CONFIG/PORTSC/IMAN/ERDP/HCSPARAMS,k-前缀）+ constexpr `scratchpad_buf_count` + port 偏移公式（`kBaseOffset=0x400`/`kSpacing=0x10`）。
- test_xhci.cpp(NEW):6 用例（3 sizeof + 3 scratchpad 解码）。

## 关键陷阱 / 诚实标注

- **PORTSC speed/power/link-state 位留 3B**:PS/PP/PLS spec 位置易记错,不猜 —— 3B 端口 reset + 测速时对照 QEMU xHCI 实测再加。
- 64-bit 寄存器（CRCR/DCBAAP/ERSTBA/ERDP）拆 `_lo/_hi` 32 位 halves（MMIO 安全,避免非对齐 64-bit 访问）。
- `Crcr::kCmdStop`（bit3 写）与 `kRingRunning`（bit3 读）同位（CS 写 / CRR 读共享 bit3）,命名区分读写语义。

## 验证

- host:test_xhci **6 例**（3 sizeof static_assert + 3 scratchpad）。
- run-kernel-test:**928/0**（头未进 kernel build）。
- kernel 工具链编译验证留 1C（首个引用的 .cpp）。

## 遗留

- 1C:控制器 init（使能 PCI + 映射 BAR0 + BIOS handoff + reset）,首引此头。

---

commit：（本次,批1B）。
