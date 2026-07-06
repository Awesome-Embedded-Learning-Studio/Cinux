# M7: VirtIO Net 网卡

> F5 设备驱动第七里程碑。基于 M2 VirtIO 传输层(virtqueue)实现 virtio-net 驱动(NetDevice)。**与 M2 同弧**(`feat/f5-virtio-blk-net`),批4-5。接 F5-M6 e1000 ✅(NetDevice + RX/TX + 中断驱动范式)/ F7 网络栈 ✅(协议层不动,挂 net_init)。

## 范围栅栏(不投机)

**做**:virtio-net RX/TX virtqueue + `VirtIONetDevice : NetDevice`(抄 e1000)+ net_init 注册 + 挂 SLIRP/loopback + **polling 跑稳后真异步 MSI-X(3 向量 RX/TX/config)+ SMP 验证**(用户约束)。

**不做**:mergeable buffer(先 fixed header `virtio_net_hdr`);多队列(单 RX/TX);checksum offload 谈判(先不支持);控制队列(除 MAC 读取外最小用);scatter-gather TX(先单缓冲)。

## 现有基建复用

| 基建 | 文件 | 复用 |
|------|------|------|
| VirtIO 传输层 | `virtio.hpp` / `virtqueue`(M2 批1) | RX/TX 两个 virtqueue |
| NetDevice 接口 | `net/net_device.hpp` | VirtIONetDevice 实现 |
| 协议栈 | `net/{arp,ipv4,icmp,udp,tcp,net_stack}.cpp` ~4500 行 | 零改动,挂 net_init |
| e1000 范式 | `net/e1000.cpp` RX/TX + 中断 | 抄收发结构 |
| MSI-X 多实例 | `msix_controller.cpp` | 第 4 实例,Table @+0x86000,3 向量 |

## 批表

| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 4 | virtio-net 驱动(RX/TX queue + NetDevice + net_init 注册)polling + SLIRP ping smoke | ⏳ | 两 leg + ping 10.0.2.2 + loopback echo |
| 5 | ⭐真中断(3 向量 0x43-0x45)+ SMP + Spinlock RX/TX buffer + perf virtio-blk vs NVMe vs AHCI + 收官 | ⏳ | 两 leg(-smp 2)+ ping + perf 数据 |

## 风险 / 陷阱

- **⭐ polling→真中断(用户约束)**:同 M2,polling 稳后紧接真 MSI-X。net 比 blk 多 3 向量(RX/TX/config),ISR 分派。
- **⭐ -smp 章态**:RX/TX buffer 池 + virtqueue 环第一天 Spinlock。e1000 是 polling(避了),virtio-net 真中断更易踩。
- **fixed header 先行**:`virtio_net_hdr`(10B + 2 num_buffers)先 fixed,mergeable buffer 留续(简化 RX 缓冲管理)。
- **向量避撞**:0x43(RX)/0x44(TX)/0x45(config),避 0x41/0x40/0x30/0x42(blk)。
- **SLIRP 时序**:e1000 踩过(test kernel 关中断→SLIRP 不投递);virtio-net 真中断后走 ISR 唤醒 net_poll kthread,时序更稳。
- **不拆 e1000**:virtio-net 独立 SLIRP netdev(id=virtio-net0),与 e1000 net0 共存,机制测各自 find。
