# F7-M4 批2 — loopback UDP round-trip 内核测

> 2026-06-30。把批1 的 UDP 协议层在**内核里**跑一个确定性 round-trip(无 SLIRP)。
> 镜像 [test_ping_loopback](../../kernel/test/test_net.cpp):同样的 LoopbackDevice +
> 单次 `poll()` 跑完整往返,只是把 ICMP echo 换成 UDP datagram。

## 做了什么

[test_net.cpp](../../kernel/test/test_net.cpp) 加 `test_udp_loopback`:注册 UDP 进
Ipv4Module 的 L4 表(`ipv4.add_l4(kIpProtoUdp, udp)`),bind 一个内核侧 `UdpCapture`
listener(定长 128B buf,freestanding 不用 std::vector),`udp.send` 发 6 字节到
127.0.0.1:7777,单次 `stack.poll()` 跑完 send→IPv4→L4 表→UdpModule→listener,断言
calls/src_port/len/payload 全对。

内核侧 listener 用 `static` + 定长 buf(同 IcmpModule 的 reply 计数器思路),不走堆容器。

## 为什么这样测

- **确定性**:loopback 无 SLIRP 时序,单次 poll 必跑完往返(躲 e1000 RX 时序坑)。
- **覆盖真路径**:UDP 在内核里走的是和生产一样的 NetStack→Ipv4Module→L4 表→UdpModule
  全链路,伪首部校验和经真实 round-trip 存活(host 测已证,batch 2 在内核态再证一遍)。
- 和 `test_ping_loopback` 并列注册,ICMP + UDP 都在 net 段。

## 验证

- `run-kernel-test-all` 两 leg 各 **968 passed, 0 failed**(967 基线 + 1 UDP)。
- 两 leg 都打 `[net] loopback UDP: 6 bytes from port 1234 (round-trip in one poll)`。
- exit 0,零 panic/fail。
