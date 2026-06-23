# 2026-06-23 F5-M5 批0B — MSI-X Table/PBA 映射 + 条目编程 + 使能

## 背景 / 目标

0A 落 MSI-X 能力发现。0B 把发现结果变成**可编程**:map Table/PBA 进 KMEM_MMIO（`+0x21000/+0x22000`,PCD）,mask 全部条目,`program_vector` 写 xAPIC addr+data,`enable` 置 Message Control Enable。纯部分（MsixTableEntry 布局 / xapic addr·data / msg-ctrl 位）host 可测;glue（`g_vmm.map` + `pci_write`）kernel-only（msix_controller.cpp,不链 host 测）。**无调用方**,真"向量触发"证明留 2C。

## 变更

- msix.hpp:+ `MsixTableEntry`（16B static_assert）+ `kEntryMaskBit` + 纯 helper 声明（`xapic_message_address/data`、`message_control_with_enable/unmask_function`）+ `MsixController` 类（init/mask_all/program_vector/enable）。`PCIDevice` 改**前向声明**（头部 const& 形参不全类型够,IWYU 净）。
- msix.cpp:+ 4 个纯 helper 实现。
- msix_controller.cpp（NEW,kernel-only）:MsixController 实现。map Table（table_size×16B）+ PBA（table_size/8B）逐页 `PRESENT|WRITABLE|PCD`;program_vector 先 mask→写 addr/data→read-back→unmask;enable 整 dword 写（保留 cap id+next 低 16 位）。
- drivers/CMakeLists.txt:+ msix_controller.cpp（CINUX_USB gate）。
- test_msix.cpp:+6 用例（sizeof==16 / xapic addr(0)=0xFEE00000 / addr(5)=0xFEE05000 / data(0x40)=0x40 / with_enable(0x0003)=0x8003 / unmask_function(0x4003)=0x0003）。

## 关键陷阱（GOTCHA）

- **enable 不能只写 MC word**:Message Control 在 `cap_offset+2`（非 dword 对齐）,`pci_write` 会 `&0xFC` 落到 cap_offset、把 cap id+next 低 16 位一起覆盖。解:读整个 dword0、改高 16 位（MC）、整 dword 写回,保留低 16 位。
- **PCIDevice 前向声明**:msix.hpp 头部 `init(const PCIDevice&)` 只需前向声明（IWYU）,full type 仅 msix_controller.cpp 用（直含 pci.hpp）。

## 验证

- host:test_msix **17 例**（0A 11 + 0B 6）全过。
- kernel:全量构建绿（msix_controller.cpp 干净编译,零新警告）。
- run-kernel-test:**928/0**（无调用方,基线零改动）。
- 诚实标注:MsixController 端到端（map→program→enable→真中断）**未验证**,留 2C。

## 遗留

- 0C:向量安装 helper（手挑空闲向量 ~0x40 + ISR stub + `g_idt.set_handler`）,把 MsixController.program_vector 的 vector 参数与真 IDT 向量接上。

---

commit：（本次,批0B）。
