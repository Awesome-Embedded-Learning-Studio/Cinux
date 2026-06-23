# 2026-06-23 F5-M5 批3B — Address Device 状态机【端口 reset + Enable Slot + 寻址,真机设备 addressed】

## 背景 / 目标

**整里程碑最高危批。** 3A 给了纯编码层(已 commit 9bfd7a4);3B 把它接上真 HW,完成 xHCI 最难单命令 Address Device:端口 reset → Enable Slot → 建 input context → Address Device → 设备获地址。验证一个 QEMU USB 设备(usb-kbd/mouse)走通 connect→reset→addressed。

## 变更

- **xhci_registers.hpp**(Portsc,+~35 行):补 PORTSC 位(power/link-state/speed/change)从 Linux xhci-port.h 核实——`[8:5]` LinkState(U0/Polling/HotReset/Resume)｜`[9]` PortPower｜`[13:10]` Speed(同 3A UsbSpeed)｜change flags CSC/PEC/RC/PLC。+ `portsc_link_state()`/`portsc_speed()` 纯提取器。
- **xhci_trb.hpp**(+~30 行):CCE 解析 `cmd_completion_slot_id(control [31:24])`/`cmd_completion_code(status [31:24])` + Address Device TRB builder(`slot_id_for_trb()`=`slot<<24`、`kBlockSetAddress` BSR)+ `CompCode{kSuccess=1,…}`。
- **xhci_controller.{hpp,cpp}**(+~70 行):`last_cmd_completion_` 成员(poll_events 存最近 CCE)+ **`run_command(param,status,control)→ErrorOr<Trb>`** 同步命令(记命令 TRB phys→submit→轮询 poll_events 直到 CCE.parameter 匹配→返回 CCE;超时 TimedOut)+ 端口助手(`portsc()`/`read_portsc()`/`port_reset()→speed`/`dcbaa_set()`)。
- **xhci_slot.{hpp,cpp}**(NEW):`XhciSlot` —— device-context(64B slot+EP0)+ input-context(96B ICC+slot+EP0)+ EP0 transfer ring(16 TRB)DmaBuffer;`allocate(slot_id)` alloc 三缓冲 + init EP0 TrbRing;`build_address_input(speed,rh_port,ep0_maxpacket)` 用 3A 编码器填 ICC + slot + EP0 context。
- **kernel/test/test_xhci.cpp**:`test_address_device`(扫 port CCS→reset→Enable Slot 读 slot_id→alloc slot + DCBAA 登记→build input→Address Device→断言 completion Success + 设备 context slot_state==Addressed)。
- **test/unit/test_xhci.cpp** section 6:PORTSC/CCE/Address Device TRB host 测(+6 例)。
- **drivers/CMakeLists.txt**:`CINUX_USB` gate 加 `usb/xhci_slot.cpp`。

## 关键陷阱（GOTCHA,致命)—— Input Control Context 的 Add/Drop flag 顺序

- **ICC DW0=Drop flags、DW1=Add flags(我搞反了)** ⚠️:3A/3B 一开始把 add-flags 写到 DW0(in[0]=0x3)、drop 写 DW1。后果:Address Device 返回 **completion code=5(TRB Error)**。**根因坐实**:抓 QEMU `hw/usb/hcd-xhci.c` 的 Address Device 校验 `if (ictl_ctx[0] != 0x0 || ictl_ctx[1] != 0x3) → CC_TRB_ERROR`——即 QEMU 要求 **DW0(drop)==0 且 DW1(add)==0x3**。xHCI spec + Linux `struct xhci_input_ctrl_ctx { drop_flags; add_flags; }` 三方一致:**Drop 在前(DW0)、Add 在后(DW1)**。修:`in[0]=0; in[1]=input_add_flag(0)|input_add_flag(1)`。改完 completion code=1(Success)、设备获地址 1、slot_state=2(Addressed)。顺带订正 3A `xhci_context.hpp` 的 InputControlContext 注释(原写 add/drop,实 drop/add)。
- **grep 日志当二进制静默**:QEMU 串口 log 含 ANSI escape → `grep` 当 binary file 不显行(F4-M1 同款)。用 `/usr/bin/grep -a` 强制文本;外加本机 `grep` 被 shell function 拦,用绝对路径 `/usr/bin/grep`。
- **CCE 字段 word 归属**:slot_id 在 control [31:24]、completion code 在 status [31:24]——抓 QEMU 源码 + Linux 宏确认(非猜),真机断言(slot_id ∈ [1,MaxSlotsEn] + code==Success + slot_state==Addressed)兜底全过。

## Address Device 流(状态机,实测通过)

1. Detect:扫 PORTSC[0..max_ports] 找 CCS。→ port 4(USB2 口,usb-kbd)。
2. Port reset:`port_reset()` 置 PR(+PP)→ 轮询自清 → 读 [13:10] speed。→ speed=3(HS),PE=1。
3. Enable Slot:`run_command(kEnableSlot)` → CCE → `slot_id=1`。
4. Alloc slot:`XhciSlot::allocate(1)` + `dcbaa_set(1, device_ctx_phys)`。
5. Build input:`build_address_input(speed=3, rh_port=5, ep0_maxpacket=64)`(HS→64)。
6. Address Device:`run_command(in_ctx_phys, 0, kAddressDevice|slot_id<<24)`(BSR=0)→ CCE → **code=1 Success**。
7. Verify:设备 context dev_state [31:27]=**2 Addressed**,dev_addr=**1**。

## 验证

- run-kernel-test:**930/0 ALL PASSED**(默认无 xHCI,枚举测 skip 计 pass;+1 测)。
- run-kernel-test-xhci:**930/0 ALL PASSED** ★ —— `address device -> completion code=1`,`device slot_state=2 dev_addr=1`。**真机端到端:USB 设备被 reset + enable slot + addressed**。
- test_host:**100% passed,0 failed out of 53**(section 6 PORTSC/CCE/TRB-builder +6)。
- 全量 build 绿 + clang-format + 全文件 <500(controller.cpp 339 / slot.cpp 90 / test_xhci.cpp 172)。

## 诚实标注

- **活中断送达(handler)仍留 5A**:3B 的命令完成靠轮询 `poll_events`(测试内核无 sti),不依赖活中断。
- 设备 context 用 slot+EP0(64B);4A interrupt EP 走 Configure Endpoint 时如需扩再议(HCCPARAMS1 CSZ=0,QEMU 32B context)。
- usb-kbd 在 qemu-xhci 上 PORTSC 报 speed=3(HS);EP0 maxpacket 按 HS=64 正确(Address Device Success 证)。

## 遗留

- **3C**:控制传输 stage TRB 构建(SETUP/Data/Status + **TRT 方向对照真实 HW 验**)→ GET_DESCRIPTOR + SET_CONFIGURATION。

---

commit：（本次,批3B）。
