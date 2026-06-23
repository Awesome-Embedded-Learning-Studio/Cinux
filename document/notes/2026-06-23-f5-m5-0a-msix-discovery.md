# 2026-06-23 F5-M5 批0A — PCI MSI-X 能力发现（纯 reader 注入）

## 背景

F5-M5 xHCI USB 里程碑的第一批。目标：点亮 USB HID 鼠标+键盘喂 GUI 事件队列,正经做 **MSI-X**（用户决策,不走 INTx 捷径）。MSI-X 全树 0 行,从零建;做成 `kernel/drivers/pci/` 下可复用件（AHCI/ext2 将来可复用）。批0A 只做**发现**（cap 遍历 + Table/PBA 解码）,映射+编程（0B）+向量安装（0C）后续。完整计划见 `~/.claude/plans/synthetic-meandering-cascade.md`。

## 目标

`msix::find_capability(bus,slot,func,reader)` —— 遍历 PCI 能力链表定位 MSI-X（id 0x11）,解析 Message Control / Table Offset·BIR / PBA Offset·BIR。**纯函数**:配置空间访问经注入的 `ConfigReader` 回调（签名同 `PCI::pci_read`）,kernel 传 `&PCI::pci_read`、host 单测传 mock → 同一份代码两处跑,不复制逻辑。

## 设计

- **reader 注入**:`using ConfigReader = uint32_t(*)(uint8_t,uint8_t,uint8_t,uint8_t)`。避免像 `test_mouse.cpp` 那样在 host 复制逻辑;直接 link 真 `msix.cpp` + mock reader 测真代码。
- **遍历**:读 STATUS（0x06,bit4 = cap list 在）→ 读 0x34（首 cap 偏移）→ 沿 next 指针走链表找 id 0x11。`kMaxCapabilities=48` 硬上限防脏数据死循环。
- **解码**:MSI-X cap 的 dword0 = `[id|next|msg_ctrl_lo|msg_ctrl_hi]`（一次读给 id+next+Message Control）;`+4` = Table Offset（31:3）+BIR（2:0）;`+8` = PBA Offset+BIR。Table Size = `MC[10:0]+1`。

## 变更

- NEW `kernel/drivers/pci/msix.hpp`（90 行）:`MsixCap` 结构 + `find_capability` 声明 + `MsixMsgCtrl`/`MsixBarOffset` 常量（k-前缀）。
- NEW `kernel/drivers/pci/msix.cpp`（73 行）:遍历+解码实现,纯,仅含 `pci_config.hpp`。
- MOD `kernel/drivers/pci/pci_config.hpp`:加 `PciReg::CAPABILITIES_POINTER`、`PciClass::{SERIAL_BUS,USB_SUBCLASS,XHCI_PROG_IF}`、`PciCmd::{IO_SPACE,BUS_MASTER,MEM_SPACE}`、`PciStatus::CAP_LIST`、`PciCapId::{POWER_MANAGEMENT,MSI,MSI_X}`。
- NEW `test/unit/test_msix.cpp`（177 行）:mock 64-dword 配置空间 + reader,11 用例（found mid-list / table size 编码 / Table·PBA Offset+BIR / 低 3 位是 BIR / 三种 not-found / 跳过 MSI）。
- MOD 构建:`option(CINUX_USB)`（顶层,默认 ON）+ `drivers/CMakeLists.txt` gate（msix.cpp）+ `test/CMakeLists.txt`（`add_cinux_integration_test(msix ...)` + ALL_HOST_TESTS）。

## 关键陷阱（GOTCHA）

**`using` 声明不能以命名空间名为目标**（本机 GCC）。`using cinux::drivers::pci::PciReg;`（PciReg 是 namespace）报 `'PciReg' has not been declared`;但对 struct/函数（MsixCap/find_capability）正常。解:host 测试改 `using namespace cinux::drivers::pci;`（directive,把嵌套命名空间名也引入）。级联效应:PciCapId 未解析导致 `cfg_put_cap` 调用点失败 → 触发假 `-Wunused-function`（修好 PciCapId 即消）。

## 验证

- **host**:`ctest -R msix` —— 11 用例全过（测真 `msix.cpp`,非副本）。
- **kernel**:全量 `cmake --build build` 绿（msix.cpp 在 freestanding kernel 工具链下干净编译,仅依赖 `pci_config.hpp` 常量）。
- **run-kernel-test**:**928/0**（基线零改动——msix.cpp 编入 `big_kernel_common` 但未被调用,pure dead code）。
- **CI 盲区**:本批新增 host 测试 + kernel 源,故跑了全量构建 + `test_msix`（非仅 run-kernel-test）。

## 遗留

- 批0B:MSI-X Table/PBA MMIO 映射（`+0x21000/+0x22000`,PCD）+ 条目编程（xAPIC addr `0xFEE00000` + vector）+ 使能（Message Control Enable bit + Function Mask）。
- 批0C:向量安装 helper（手挑空闲向量 ~0x40 + ISR stub + `g_idt.set_handler`）。"向量真触发"证明挪到批 2C（doorbell NOOP→Command Completion Event 自然触发）。
- MSI-X 命名空间 `cinux::drivers::pci::msix` 放 pci/（通用）,xHCI 主体后续放 `kernel/drivers/usb/`。

---

commit：（本次,批0A）。
