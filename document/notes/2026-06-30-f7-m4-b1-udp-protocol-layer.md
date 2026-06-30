# F7-M4 批1 — UDP 协议层 + IPv4 L4 proto 表(ICMP 迁入)

> 2026-06-30。F7 网络栈 M4 UDP 第一批:协议层底子(host 单测守),把 IPv4 的
> L4 分派从「硬编码 ICMP」升级成 proto→handler 表,UDP 挂在同层。

## 背景

F7-M1/M2/M3 已合 main:netdev 抽象 + ARP + IPv4/ICMP,真 ping `10.0.2.2` 干通。
[ipv4.cpp:72-75](../../kernel/net/ipv4.cpp) 当时为 ping 硬编码 `if(ip.proto==ICMP)
icmp_.handle(...)`,并留 TODO:`proto->handler table for UDP/TCP`。M4 加 UDP,正好
还掉这笔债——单一 L4 分派机制,不再硬编码。

范围栅栏(socket API 留 F7-M6 / TCP 留 F7-M5 / ring3 ping #DF 是另一条线):本批只做
**协议层**——UDP 封装(头 + 伪首部校验和)/ 解析(proto=17 分派)/ 端口多路复用,加
host 单测。不进生产 `net::init()`(无消费者,避免死接线)。

## 设计

### 决策 A:IPv4 接 UDP —— proto 表(还掉 TODO)

加 `L4Handler` 缝([ipv4.hpp](../../kernel/net/ipv4.hpp)),签名同 `ProtocolHandler`
(ethertype 缝)下一层:`handle(Ipv4Header&, FrameView, NetDevice&, Ipv4Module&,
NetStack&)`。`Ipv4Module` 持 `L4Slot l4_[kMaxL4=4]` 小表 + `add_l4(proto, handler)`
(写法照搬 `NetStack::protos_`)。**ICMP 在 ctor 里自动 `add_l4(1, icmp)` 进表**——所以
现有 call site(`Ipv4Module ipv4(icmp, &arp)`)零改;UDP 用 `ipv4.add_l4(17, udp)` 注入。
`on_frame` 查表分派,删掉硬编码 if + TODO。

考虑过「兄弟分支」(再加 `if(proto==17)`),否决:留两套分派机制是 §13 味道,TODO 更扎眼。
表方案对 proven ping 路径的改动有双保险:`test_ping_loopback` + `test_ping_e1000` 两 leg
都打 ICMP,迁错立刻红(本批验证实证两 leg 仍 967/0,且 `[net] ...ping reply` 全通)。

**GOTCHA(编译)**:ctor 里 `add_l4(kIpProtoIcmp, icmp)` 要 `IcmpModule& → L4Handler&`
派生→基类转换,需 IcmpModule 完整类型;但 ipv4.hpp 只前向声明 IcmpModule(不能 include
icmp.hpp——循环)。所以 **ctor 定义挪到 ipv4.cpp**(.cpp include icmp.hpp,完整类型可见),
头里只留声明。这是唯一一处不顺手的地方。

### 决策 B:UDP 校验和 —— 连续缓冲区法

UDP 校验和覆盖 [伪首部 12B | UDP 头 8B | payload]。TX 把三段拼一个 `HeapBuf`,
`internet_checksum(buf, 20+n)` 一把出值,嵌进 UDP 头校验和字段(计算时该字段=0),
再发 UDP 段(跳过伪首部,伪首部不上线)。RX 同构重建 [伪首部 | 收到的 l4] 跑
`verify_internet_checksum`。比 `pseudo_header_partial` + 手搓 16 位累加少一道弯
(internet_checksum 自带 fold+complement),且 known-vector 测已守。

细节:`checksum=0` 按 RFC 768 当「无校验和」跳过验证;TX 算出 0 发 0xFFFF(0 是「无」)。
长度用 UDP 头的 `h.length`(非 delivered 字节数)重建伪首部 + 求和,匹配 sender。
src IP 从 `stack.config_for(dev)->local` 取(同 ipv4.send)。

### 端口多路复用

`UdpModule` 内 `PortSlot ports_[kMaxUdpPorts=16]` + `bind/unbind`。`UdpListener::
on_udp(ip, src_port, payload)` 是观察缝(payload 借用,handler 期间有效)。16 槽是协议层
够用;socket 层(F7-M6)再扩到 256。

## 验证

- host `test_net_udp`(新,9 例):头 round-trip / TX 线序(proto=17 + 端口 + 长度)/
  send→IPv4 表→handle→listener 完整 round-trip(无 QEMU/SLIRP,no-L2 mock 把 send 捕获
  的 IP 包直接喂回)/ 校验和损坏丢弃 / checksum=0 接受 / 无 listener 丢 / 双端口 demux /
  unbind / 重复 bind 拒。
- `ctest` 63/63(62 基线 + net_udp)。
- `run-kernel-test-all` 两 leg 各 **967/0**;ICMP 走新表实证:`[net] loopback ping
  reply id=0xabcd` + `[net] e1000 ping 10.0.2.2 reply id=0x1234` + production ping +
  sys_ping 全通。
- udp.cpp 在内核 -Werror freestanding 下零警告编译通过。

## 文件

- 新:[udp.hpp](../../kernel/net/udp.hpp) / [udp.cpp](../../kernel/net/udp.cpp) /
  [test_net_udp.cpp](../../test/unit/test_net_udp.cpp)
- 改:[ipv4.hpp](../../kernel/net/ipv4.hpp)(L4Handler + kIpProtoUdp + 内部表)/
  [ipv4.cpp](../../kernel/net/ipv4.cpp)(ctor 注册 ICMP + on_frame 走表)/
  [icmp.hpp](../../kernel/net/icmp.hpp)(`: public L4Handler` + override)/
  net CMakeLists(链 udp.cpp)/ test CMakeLists(注册 net_udp)
