# 2026-06-23 F5-M5 批3C — 控制传输(SETUP/Data/Status)+ GET_DESCRIPTOR + SET_CONFIGURATION【设备描述符读到】

## 背景 / 目标

3B 让设备获地址(slot_state=Addressed)。3C 把控制传输打通:EP0 上发 SETUP→Data→Status 三阶段(或无数据两阶段),读设备描述符 + 配置设备。**3A deferred 的 TRT 方向歧义**(Linux `TRB_DATA_OUT=2/IN=3` vs spec 记忆相左)本批必须坐实。

## 变更

- **xhci_trb.hpp**:
  - **修 2A 位常量 bug**(高危,见下 GOTCHA):`kImmediateData` 1<<4→**1<<6**、`kInterruptOnCompletion` 1<<2→**1<<5**、`kChain` 1<<5→**1<<4**;bit2 实为 ISP(`kInterruptOnShort`);加 `kDataDirIn`=1<<16。
  - `trb_control` 改 constexpr。
  - **控制 stage builder**(纯 host 可测):`setup_stage_control(trt)`(type2+IDT+CHAIN+TRT[17:16])、`data_stage_control(in)`(type3+CHAIN+DIR/ISP)、`status_stage_control(data_in)`(type4+IOC,方向=数据反向)。`Trt{kNone=0,kOut=2,kIn=3}`、`kSetupStageLength=8`。
  - Transfer Event 解析:`transfer_event_epid(control [20:16])`、`transfer_event_remaining(status [23:0])`(code/slotid 复用 CCE 的)。
- **xhci_controller**:`last_transfer_event_` 成员(poll_events 见 Transfer Event 存之)+ **`run_transfer(slot_id, epid)`**:doorbell `doorbells_[slot_id]=epid`(EP0=1)+ 轮询 poll_events 直到 Transfer Event 匹配(slotid+epid)→ 返回该 event。
- **xhci_slot.{hpp,cpp}**:`data_buf_` DmaBuffer(256B,描述符读落此)+ `control_in(hc,setup,len)`(三阶段 IN:SETUP(TRT=3)+Data(IN)+Status(OUT))、`control_out_no_data(hc,setup)`(两阶段:SETUP(TRT=0)+Status(IN))、便捷 `get_descriptor(hc,type,index,len)`/`set_configuration(hc,value)`。读 Transfer Event 的 completion code(Success/ShortPacket 都接受)+ actual=len-remaining。
- **kernel/test/test_xhci.cpp**:扩 test_address_device——Addressed 后 GET_DESCRIPTOR(Device,18) 读描述符(断言 bLength=18/type=1,打印 vid/pid/class/ncfg)+ SET_CONFIGURATION(1)(断言成功)。
- **test/unit/test_xhci.cpp** section 7:stage builder 控制字(setup/data/status 精确值)+ Transfer Event 解析(+5 例)。

## 验证工作流(ultracode:多源 fan-out + 对抗验证)

跑 8-agent workflow 核实控制传输 TRB 格式 + TRT 方向(QEMU hcd-xhci.c[测试目标]+ Linux xhci.h/xhci-ring.c + spec),**3/3 对抗 lens 确认**:
- **TRT 映射:`2=OUT`(host→device)、`3=IN`(device→host)、`0=无数据`**(3A「2=IN」的记忆反了)。QEMU 不读 TRT(从 SETUP bmRequestType bit7 取方向),故 QEMU 上装饰性但真 HW 正确。
- **抓到 2A 位常量 bug**(独立用本地 Linux xhci.h 复核 `TRB_IDT=BIT(6)`/`TRB_IOC=BIT(5)`/`TRB_CHAIN=BIT(4)` 三方一致)。
- 完整 TRB 链(GET_DESCRIPTOR 三阶段 / SET_CONFIGURATION 两阶段)控制字。
- Transfer Event:type32、control[31:24]slotid/[20:16]epid、status[31:24]code/[23:0]**剩余**字节。

## 关键陷阱(GOTCHA)

- **2A TRB control 位常量全错** ⚠️:`kImmediateData=1<<4`(应 1<<6)、`kInterruptOnCompletion=1<<2`(应 1<<5)、`kChain=1<<5`(应 1<<4)。2A-2C 没暴露(NOOP/Link TRB 没用到这些位)。后果:SETUP TRB 设 IDT=bit4 → QEMU `trb_setup->control & TRB_TR_IDT`(bit6)假 → "Setup TRB doesn't have IDT set" 拒绝。**修后 GET_DESCRIPTOR 立即通**。位位置三方核实(QEMU+Linux xhci.h)。
- **host 测期望值笔误 ×2**(我的):抄 synthesis 时把 TRT `3<<16=0x30000` 写成 `0x3000000`(多一位)→ 0x03000850 vs 实际 0x00030850;另漏了 data stage 的 TYPE 位。builder 一直对(用位常量),是测试 hex 数错。修后 31/0。
- **synthesis 内部不一致**:workflow 综合报告的「hex 值」与「位分解」矛盾(hex 漏 CHAIN/TYPE)。教训:信位分解 + 自己用常量重算,别直接抄 hex。

## 验证(★ 真控制传输端到端)

- run-kernel-test:**930/0 ALL PASSED**(默认无 xHCI,控制测 skip)。
- run-kernel-test-xhci:**930/0 ALL PASSED** —— `device descriptor: bLength=18 vid=0x627 pid=0x1 class=0x0 ncfg=1`(vid 0x0627=QEMU usb-kbd,GET_DESCRIPTOR 读到真描述符)+ `set_configuration(1) ok`。**USB 设备完整枚举:connect→reset→addressed→描述符读到→configured**。
- test_host:**53/53**(section 7 stage builder +5)。
- 全量 build 绿 + clang-format + 全文件 <500(controller 358 / slot 158 / test_xhci kernel 192)。

## 遗留

- **4A**:HID boot —— 读配置描述符找 interrupt-in EP + SET_PROTOCOL(boot)+ interrupt-in 轮询 + 报告解码(**鼠标 Y 轴 me.dy=+hid_dy 不取反**)。
- **5A**:生产 boot 接线(sti+APIC 证活中断送达)+ 注入 Mouse::event_queue + CINUX_QEMU_USB。

---

commit：（本次,批3C）。
