# 2026-06-23 F5-M5 批2C — MSI-X 武装 + doorbell NOOP→Command Completion Event（命令管线端到端）

## 背景 / 目标

**最高风险批。** 把 MSI-X 武装接上（start 设 MsixController→向量 0x40）,注册事件环 hook,doorbell NOOP→控制器执行→Command Completion Event→事件中断。证明 xHCI 命令管线 + 中断路径端到端通。

## 变更

- xhci_controller.{hpp,cpp}:+ 存 `dev_`/`msix_cap_`/`msix_` 成员;`start()` 加 MSI-X 武装（find_capability→init→mask_all→program_vector(0,0x40,0)→enable）+ `set_xhci_irq_hook`;+ `submit_command`(enqueue+doorbell0)+ `poll_events`(dequeue+ERDP+清 IMAN.IP)+ `event_irq_thunk`(s_instance_→poll_events)。
- msix_controller.cpp:**MSI-X Table/PBA 偏移从 +0x21000/+0x22000 挪到 +0x40000/+0x41000**(避碰,见 GOTCHA)。
- xhci_ring.hpp:+ `base()` 诊断访问。
- kernel/test/test_xhci.cpp:submit NOOP→轮询事件环→断言 cmd_completions>0 + EINT。

## 关键陷阱（GOTCHA,致命)

- **MSI-X Table/PBA 与 xHCI BAR0 碰撞** ⚠️:xHCI BAR0 映 4 页(+0x20000-0x23FFF)覆盖 cap+op+**runtime(BAR0+RTSOFF=+0x1000→+0x21000)+ doorbell(BAR0+DBOFF=+0x2000→+0x22000)**。原 MSI-X Table(+0x21000)/PBA(+0x22000)正好覆盖 xHCI 运行时寄存器 + 门铃!后果:`ir0_` 读到 MSI-X Table 内存、门铃写进 PBA 区→命令环永不执行(compl=0)。诊断暴露:db0/cmdTrb0 正常但 compl=0 + CRCR CRR=0。**修:MSI-X 挪 +0x40000/+0x41000(在 BAR0 4 页外)**。这是 2B "running" 却 2C 全黑的根因——2B 只查 op_regs(+0x20040,不在碰撞区),IR0 写入其实全写错地方(事件环没真武装),但 RS=1 仍让控制器 running。
- **IMAN.IE 必须在 USBCMD.RS 之后再设**:RS 翻转前设 IE 会被清(IMAN=0x0)。改到 RS 后设→IMAN=0x2(IE=1)。
- **IMAN.IP 读时序怪**(QEMU 模型):compl=1 + EINT=1(IP 之或)却 IP=0。改用 EINT(USBSTS bit3)作"中断 RAISE"的铁证,不依赖 IP 单读。

## 验证(★ 命令管线 + 中断路径端到端)

- run-kernel-test-xhci:**929/0** —— `command pipeline: cmd_completions=1 EINT=1 USBSTS=0x8`。**NOOP 执行→Command Completion Event 出→事件中断 RAISE(EINT)**。
- 默认 run-kernel-test:929/0(test_xhci 跳过)。
- **诚实标注**:活的中断送达(handler 跑 / g_xhci_irq_count++)需 sti+APIC,测试内核没有→留 5A 生产内核证明。控制器侧中断路径(EINT)已证。

## 遗留

- 3A:slot/context + 控制传输(Address Device 需要 input context)。
- 5A:生产 boot(sti+APIC)证明活中断送达 + 真鼠标。

---

commit：（本次,批2C）。
