# F-GUI 批4 keyboard 无响应修复 — ERDP.EHB RW1C 漏清

**日期**:2026-07-07　**分支**:`worktree-gui-userspace`　**commit**:`bd90833`
**前置**:批3b 删内核 `gui_worker` 线程替用户态 host 进程后,keyboard 全卡(详见 handoff `f-gui-b4-keyboard-msix-handoff`)。本批治本。

## 现象

GUI 起来猛敲键盘:**零响应**(`[EVT] key push` = 0)。mouse 偶尔动一下(cursor 偶尔更新)。点窗口关闭按钮无反应。

## 根因(钉死)

`XHCIController::poll_events()` 写 ERDP 漏 `| Erdp::kEventHandlerBusy`。

**ERDP bit3 (EHB = Event Handler Busy) 是 RW1C —— 写 1 清,写 0 保持**。原代码:

```cpp
const uint64_t erdp = event_ring_buf_.phys() + dequeue_index * 16;  // bit3 恒 0
ir0_->erdp_lo = (uint32_t)erdp;   // 写 0 = 不清 EHB
ir0_->erdp_hi = (uint32_t)(erdp >> 32);
```

外加原注释自承 *"the 16-byte-aligned ptr clears EHB"* —— **判反了 RW1C 语义**(代码作者也以为 16-byte 对齐让 bit3=0 就清 EHB;实际写 0 = 保持)。

第一次 MSI-X IRQ 时 HC 写 event 进 ring 同时 set EHB=1;`poll_events` 写 ERDP bit3=0 不清 EHB → interrupter 一直 busy → 后续事件入 ring 不再 re-assert IP → **MSI-X 只 fire 1 次(boot 期间),之后死**。keyboard/mouse 后续 transfer complete 事件堆在 ring 里无人 poll → 全卡。mouse 偶尔动 = boot 那次 dequeue 到一个 mouse report 的副作用。

还漏清 `Usbsts::kEventInterrupt`(W1C,controller-level event interrupt status,Linux IRQ 路径会清)。

## 诊断过程(数据驱动 + 对抗裁决)

加 3 个 kprintf 仪器(production GUI run 猛敲键盘实测):

| 仪器 | 输出 | 判读 |
|---|---|---|
| MSI-X entry0 回读 | `addr_lo=0xfee00000 data=0x40 ctrl=0x0` | ✅ Table 写对(dest=0 BSP, vec=0x40, unmask) |
| IMAN post-enable | `IE=1 IP=0 raw=0x2` | ✅ IE latched(推翻源码注释 *"QEMU+nested-KVM IMAN.IE does not reliably latch"*) |
| `[xHCI IRQ #N]` 前 8 次 | **只 `#1` 1 次**(boot,之后猛敲 0) | ⚠️ MSI-X deliver 1 次后停 |

配置全对但 fire 1 次停 → 锁 interrupter **ack 协议**,非 MSI-X 玄学。

我原倾向 **B(poll kthread 固化债)**。用户找大 AI 裁决,大 AI 一刀切中:**ERDP.EHB 是 RW1C,清它要写 1 不是写 0,你判反了**(引用 Linux `xhci-ring.c` `temp_64 |= ERST_EHB`)。改判 **C(治本)**。

## 修法(`bd90833`)

`poll_events` 末尾 ack 块,顺序对齐 Linux `xhci-ring.c`(EINT → IP → EHB):

```cpp
op_regs_->usbsts = Usbsts::kEventInterrupt;              // W1C: controller-level EINT
ir0_->iman       = ir0_->iman | Iman::kPending;          // W1C: interrupter-0 IP
const uint64_t erdp_ack = erdp | Erdp::kEventHandlerBusy;// RW1C: 写1清 EHB
ir0_->erdp_lo    = static_cast<uint32_t>(erdp_ack);
ir0_->erdp_hi    = static_cast<uint32_t>(erdp_ack >> 32);
```

订正错误注释(原"aligned ptr clears EHB"删,加 CRITICAL 说明 RW1C 语义)。

## 排除的嫌疑

- ❌ **漏 IST2**:xHCI(0x40)IST=2 在 F13-B(`54f6392`)早修(`irq_handlers.cpp:172`)。memory 老交接"漏 IST2?"的猜测被反证。
- ❌ **MSI-X Table 没写**:仪器 1 证伪(addr/data/ctrl 全对)。
- ❌ **IMAN.IE 没 latch**:仪器 2 证伪(IE=1)。
- ❌ **QEMU+TCG MSI-X 固有坏**:NVMe(0x41)同构 `MsixController::program_vector` work(production boot disk 高频 fire)+ Linux 同 QEMU/TCG 纯中断 work。
- ✅ **ERDP.EHB RW1C 漏清 + USBSTS.EINT 漏清**

## 验证

GUI run:**keyboard 打字 + mouse click 关窗 + shell 目录映射全通**(mouse 偶尔动也同根,EHB 清对后整条 event ring 活过来,所有依赖 mouse 的交互连带通)。console gate `run-kernel-test-all` 两腿 917/907 passed 0 failed 无 panic(清仪器 + format 后零回归)。

## ⭐ 教训

1. **RW1C = write-1-to-clear**(非 write-0)。我判反了,代码注释也判反了。对 MMIO 的 W1C status bit,写 1 才清,写 0 保持。ERDP.EHB / IMAN.IP / USBSTS.EINT 都是 W1C。
2. **代码注释会撒谎**。原注释"aligned ptr clears EHB"是想当然的错。协议细节得对 spec / Linux 源码核,不轻信注释。
3. **别怀疑 QEMU**。Linux 同环境纯中断 work = CinuxOS bug(`mechanism-test-and-debug-not-qemu` 族)。
4. **数据驱动 + 对抗裁决**。3 仪器锁"配置全对但 fire 1 次停" → 方向明确;大 AI 对抗裁决纠正我判反 RW1C(我原想 B 固化债,被否"太早")。这是"不猜测加仪器" + "外部对抗"两条方法论叠加的价值。
5. **这是 RW1C ack bug,不是 IST 族**(`f5-m2-m7-virtio-progress` / `f13-b-host-adapter-handoff` IST 是 IRQ 栈覆盖 task 栈问题,另一类),别混为一谈。

## 残留(open)

- **mm PageCache corrupt**(`f-gui-b4-shell-spawn-handoff`):nested-fork + dynamic ELF 触发 `#GP @ alloc_order`,buddy DIAG 留(worktree dirty `buddy.cpp`)。用户决定放下。下个 AI 若追:5 轮 trace 法(`mm-mapcount-munmap-cache-phys`)。
- **Desktop::render 不画 bg**:close 窗口后 staging 残留 follow-up(子模块 Cinux-GUI dirty)。
- **worktree 剩 dirty 10 文件 + 子模块**:mm/devfs/close/pty/main 的之前会话成果,未 commit(mm PageCache corrupt 没修完前 mm 不好干净 commit)。

## 关键文件

- `kernel/drivers/usb/xhci_controller.cpp` `poll_events`(`bd90833`)
- `kernel/drivers/usb/xhci_registers.hpp`:`Erdp::kEventHandlerBusy`(bit3)/ `Usbsts::kEventInterrupt`(bit3)/ `Iman::kPending`(bit0)—— 常量早定义了,本批才真用上 `Erdp::kEventHandlerBusy`
- 对照 Linux `drivers/usb/host/xhci-ring.c` `xhci_update_erst_dequeue`(`temp_64 |= ERST_EHB`)
