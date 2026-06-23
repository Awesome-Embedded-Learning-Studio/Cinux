# 2026-06-23 F5-M5 批2B — DCBAA + scratchpad + 中断器/ERST + USBCMD.RS 启动

## 背景 / 目标

把 2A 的 ring 接上真 DMA。`start()`:DCBAA（(MaxSlots+1)*8）+ scratchpad（SPB>0 时分配数组 + 页,DCBAA[0]=数组 phys）+ 命令环（CRCR）+ 事件环 + 单段 ERST + IR0 使能 + CONFIG.MaxSlotsEn + USBCMD.RS|INTE。控制器离开 HCH = running。

## 变更

- xhci_controller.{hpp,cpp}:+ `start()` + 6 DmaBuffer 成员（DCBAA / scratchpad 数组+页 / 命令环 / 事件环 / ERST）+ TrbRing/EventRing 成员。start() 用 `g_dma_pool.alloc` + `std::move(b.value())`（ahci 范式）。
- kernel/test/test_xhci.cpp:+ start() 调用 + 断言 USBSTS.HCH 清（running）。

## 关键陷阱

- **QEMU xHCI 要 15 个 scratchpad buffer**（HCSPARAMS2 解出 spb=15）。start() 必须分配 scratchpad 数组（15*8）+ 15 页 buffer + DCBAA[0]=数组 phys,否则控制器异常。`scratchpad_buf_count` 解码正确（0A/1B 的纯函数验证在此兑现）。
- **ERST 是段表**:ERSTSZ=段数（1）,不是段大小;段大小在 ERST entry[0].size（=事件环 TRB 数 64）。ERST entry 16B:{base u64, size u16, reserved}。
- **寄存器编程顺序**:DCBAAP→CRCR→ERSTSZ/ERSTBA/ERDP→IMAN.IE→CONFIG→USBCMD.RS|INTE。
- **64-bit 寄存器拆 _lo/_hi 写**（DCBAAP/CRCR/ERSTBA/ERDP）。

## 验证（★ 控制器 running）

- run-kernel-test-xhci:**929/0** —— `[xHCI] running (MaxSlotsEn=64, scratchpad=15, event ring armed)` + `bring-up test passed: run USBSTS=0x0`（HCH 清=running,CNR 清,零错误）。**scratchpad=15 证实 QEMU 要 15 个 scratchpad buffer 且全分配成功**。
- 默认 run-kernel-test:929/0（test_xhci 跳过）。

## 遗留

- 2C:接线 MSI-X→event-ring ISR + doorbell NOOP→Command Completion Event→`g_xhci_irq_count++`（中断管线端到端证明）。

---

commit：（本次,批2B）。
