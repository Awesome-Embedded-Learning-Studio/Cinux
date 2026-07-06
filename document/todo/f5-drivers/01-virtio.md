# M2: VirtIO 传输层 + VirtIO Block 驱动

> F5 设备驱动第二里程碑。VirtIO modern PCI 传输层(capability + split virtqueue)+ virtio-blk 块设备驱动(IBlockDevice)。**与 M7 VirtIO-Net 同弧**(`feat/f5-virtio-blk-net`,2026-07-06 从干净 main `17798fa`),传输层两设备共享。接 F5-M1 AHCI ✅ / F5-M3 NVMe ✅(IBlockDevice + DmaPool + MSI-X 多实例参考)/ F5-M5 xHCI ✅(MSI-X 参考)。

## 范围栅栏(不投机)

**做**:VirtIO modern transport(PCI cap 遍历 common_cfg/notify/isr/device_cfg)+ split virtqueue(desc/avail/used + submit/wait/notify)+ virtio-blk 驱动(read/write/flush + IBlockDevice 适配)+ **polling 跑稳后直接真异步 MSI-X 中断 + SMP 验证**(用户约束,不留 follow-up)。

**不做**:packed virtqueue(split 够);scatter-gather 多描述符链(先单缓冲,抄 NvmeBlockDevice);legacy transport(QEMU 默认 transitional 但走 modern cap);boot disk 切换(留 perf 批);VirtIO-Net(M7,批4-5)。

## 现有基建复用(NVMe 弧趟过)

| 基建 | 文件 | 复用 |
|------|------|------|
| PCI 框架 | `pci.hpp` `find_device` 模板 | 加 `find_virtio_block` |
| MSI-X 多实例 | `msix_controller.cpp`(`table_virt/pba_virt` 参数) | 第 3 实例,Table @+0x84000 |
| DMA | `dma_pool.hpp` / `dma_buffer.hpp` | virtqueue 三表 + 单传输缓冲 |
| IBlockDevice | `block_device.hpp` / `nvme_block_device.cpp` | VirtIOBlock 抄 NvmeBlockDevice |
| MMIO 窗 | `memory_layout.hpp` KMEM_MMIO 2MB | virtio-blk BAR @+0x80000(NVMe 占到 +0x75FFF) |

## PCI 设备识别

vendor 0x1AF4。device_id:virtio-blk transitional 0x1001 / modern 0x1042;virtio-net transitional 0x1000 / modern 0x1041。QEMU `-device virtio-blk-pci` 默认 transitional(读 0x1001)但有 modern capability。`find_virtio_block` 两 ID 都接,transport 一律 modern(legacy transport 不做)。

## 批表

| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 0 | 立项 docs + `CINUX_VIRTIO` option + QEMU virtio-blk-pci + PCI `find_virtio_block` | 🔄 | 全量编 + 两 leg 零回归 |
| 1 | 传输层(`virtio.hpp` cap + feature + status)+ virtqueue(split queue)+ polling 机制测 | ⏳ | 两 leg + cap offset 回读 + queue round-trip |
| 2 | virtio-blk 驱动(read/write/flush + IBlockDevice + main.cpp 注册)polling | ⏳ | 两 leg + round-trip byte-compare + Ext2 mount |
| 3 | ⭐真中断 + SMP(MSI-X 0x42 unmask + ISR flag/wake + Spinlock DmaBuffer/virtqueue + -smp 2) | ⏳ | 两 leg(单核 + -smp 2)+ 并发 read 不出 garbage |
| 5 | perf 对比 virtio-blk vs NVMe vs AHCI(归 M7 收官批) | ⏳ | I/O 数据 |

## 风险 / 陷阱

- **⭐ polling→真中断(用户约束)**:polling 稳后紧接上真异步 MSI-X(NVMe 停在 mask_all polling 留 follow-up,本弧不留)。ISR 只 flag+wake,**不 inline schedule**(避 #DF,同 sys-ping-df / PIT IRQ 重入)。
- **⭐ -smp 章态(NVMe `749e7db`)**:DmaBuffer + virtqueue 环第一天就 Spinlock。-smp 2 多核并发覆盖单 DmaBuffer → garbage → ext2 坏。两 leg 含 -smp 2。
- **modern cap 遍历**:common_cfg/notify/isr/device_cfg(cap_id=0x09 子类型 1/2/3/4)offset 要全。批1 机制测证 cap offset 真值(对标 NVMe 批1 假绿)。
- **向量避撞**:0x42,避 0x41(NVMe 曾污染 LAPIC)/0x40(xHCI)/0x30(LAPIC timer)。
- **smoke 默认 ON 挂死**:本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`。
- **VNC 避让**:多 AI 会话共 -vnc :0 互杀,验证临时 sed -vnc :5,跑完 git checkout。
