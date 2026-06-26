# F7 网络协议栈 L0 — netdev 抽象层 + buffer 借用模型 + host 单测

**日期**: 2026-06-26　**分支**: `worktree-f7-net-ping`(从 main `fb25c89`)　**提交**: `8ab2569`
**验证**: host 单测 4/4 + run-kernel-test 945/0 + 解耦 grep 成立

## 背景
e1000 RX/TX(PR#40)已合 main,F7 全树仍 ⏳,F5-M6 批c(netdev 抽象)延后。用户两度拍板:① **抽象要做对**(加网卡不疼)——含零拷贝借用模型 + loopback;② **不急 ping,底子优先**——e1000「梭哈」教训(批b「读 RDH 收得到包」是 filter-dump 副作用、64-trap hack 碰运气,绿了 ≠ 对了)。

## 目标
L0 = **纯抽象层 + host 单测**,不碰内核运行时、不碰 QEMU 网。loopback 当确定性试验台(L1),e1000 收尾(L2),ping 是栈正确的自然结果而非 gate。

## 设计(两轴分离)
- **NetDevice**(设备轴 / L2 seam):`mac()->bool` / `has_ethernet_header()` / `max_frame()` / `supports_zerocopy()` / `poll_rx(Packet&)` / `send_l3(next_hop, ethertype, l3, len)`。
- **ProtocolHandler**(协议轴 / L3 seam):`on_frame(L2Info, FrameView, NetDevice&, NetStack&)`,按 ethertype 注册。
- **NetStack**:唯一前端,持设备表(`kMaxDevs=2`)+ ethertype 派发表 + 预算制 round-robin 抽干 `poll()`。
- **buffer 抽象** = `Packet{data, len, sink}` + NIC 无关 `BufferSink`;`scope_guard` 兜底 recycle(三红队见下)。
- **FOLD-A**(loopback 友好):mac 可选 / `has_ethernet_header` / 设备自决 L2 帧(`send_l3` 是 L3 入口,e1000 自己 prepend EthHdr,loopback 裸发)。
- **FOLD-B**(共存友好):设备表 + `on_frame` 透传 `NetDevice&`,ARP reply 从**同 NIC** 出 → 栈里无 singleton。
- **poll() 预算制抽干**(`kPollBudget=64`):一次 poll 跑完 loopback request→reply 往返(reply 入队→下轮抽干),且防 runaway 自发 handler 挂死。
- **字节序**:多字节字段 parse 时 `(hi<<8)|lo` 进 host order(避 LE/BE 比较坑——checksum/ethertype 是头号「绿但坏」陷阱);地址 4/6 字节 memcpy verbatim。

## 决策
- **e1000 用 copy adapter**(`sink=null`,**不碰 E1000Controller**,零驱动风险);**loopback 零拷贝 RX** 证明借用通路(BufferSink recycle 归还队列槽)。virtio 零拷贝是 future(`supports_zerocopy()` 能力查询诚实声明,不假装路径已存在)。
- **kernel/net/ 零 kprintf**(纯协议库,host 可链、零 kernel 依赖);日志留 net_init / adapter。
- **大量复用 Cinux-Base**:`Span`(`FrameView`)/ `ScopeGuard`(`SCOPE_EXIT`)/ `internet_checksum`(不造轮子)。
- **TX 暂 copy**(零拷贝 TX 是 virtio 高吞吐 future,不在 F7)。
- 一个 CMake gate(`CINUX_NET`),`net_stack_stub.cpp` 留 L2 接 main 时再加。

## buffer 三红队(对抗 review 结论手工折入)
| 红队 | 场景 | 解法 |
|------|------|------|
| UAF | handler 在 `on_frame` 返回后持指针 | 契约:`FrameView` 仅 `on_frame` 内有效,**要留就 copy**;`scope_guard` 派发后必 recycle |
| drop 泄漏 | 帧被丢(无 handler / runt / 坏校验和)buffer 没归还 | `scope_guard` 在**所有**出口 recycle(handle / continue / parse 失败);copy 设备 `sink=null` 即 no-op |
| 重入 | loopback 是软件,handler 里 `send_l3` 同步重入派发打花当前 buffer | loopback send **只入队**(多槽 RX 环),**下个 poll 轮**才派发——send 不重入 dispatch |

## 验证
- **host 单测 4 个全绿**(test/unit/,链 net_stack.cpp / checksum.cpp):
  - `net_dispatch`:派发命中 + L2 解析 + 回收合约(handle/drop/runt/copy/no-L2/send_l3/drain-order,11 例)
  - `net_arp_cache`:insert/lookup/miss/update/round-robin evict/clear(6 例)
  - `net_buffer`:Packet 默认 + BufferSink recycle + scope guard(正常/早退/null)(5 例)
  - `net_checksum`:RFC1071 手算向量(`0x1234→0xEDCB` / 全 0→`0xFFFF` / 奇数长 `0x123456→0x97CB` / 进位折叠 `0xFFFFFFFF→0x0000`)+ IPv4 round-trip + 损坏拒绝(6 例)
- **run-kernel-test 945/0**:`net_stack.cpp` 进 `big_kernel_common`(CINUX_NET gate)编链通过,未接线,零回归。
- **解耦 4 grep 成立**:`kernel/net/` 零 `#include` e1000 / dma_buffer / irq(6 处 "e1000" 全是注释)。
- 文件最大 test_net_dispatch 293 行,全 <500。

## 陷阱
- `Span(nullptr, 0)` 构造歧义(`0` 既匹配 `size_t` 又匹配指针形参)→ 改默认构造 `FrameView payload;`。
- `poll()` 初版每次每设备只抽 1 帧 → 改预算制抽干(L1 loopback 往返、e1000 burst 都依赖)。
- clang-format 重排 include(Cinux-Base 与 `<cstdint>` 块序)——以 `.clang-format` 为准,不手调。

## 下一步
**L1**:ArpModule + Ipv4Module + IcmpModule + LoopbackDevice → 内核测 `test_net` ping `127.0.0.1`(确定性,不碰 SLIRP 时序)。栈在 loopback 上端到端证明后,**L2** 才接 e1000(真 ping `10.0.2.2`,失败锁死在 adapter)。
