# F7 网络协议栈 L1 — ARP/IPv4/ICMP + loopback,ping 127.0.0.1 确定性跑通

**日期**: 2026-06-26　**分支**: `worktree-f7-net-ping`　**提交**: `794147f`
**验证**: host net 单测 5/5 + run-kernel-test **946/0**(+1) + 解耦 grep 仍成立

## 背景
L0 抽象层站住后(见 [L0 note](./2026-06-26-f7-net-l0-netdev-abstraction.md)),L1 把真正的协议栈
(ARP / IPv4 / ICMP)在 **loopback** 上端到端跑通——纯软件、确定性、不碰 QEMU SLIRP 时序
(正是 e1000「梭哈」翻车的根因)。栈在 loopback 上对了,L2 才接 e1000。

## 新增模块(kernel/net/)
- **ArpModule**(ethertype 0x0806):`on_frame` 学 sender 入 cache + 答请求(同 NIC 出,FOLD-B);
  `resolve_l3(ip)` cache 命中返 MAC,miss 发 ARP request 返 false(异步,下轮 poll 重试,不阻塞)。
- **Ipv4Module**(ethertype 0x0800):校验 version/IHL/total_len/header checksum(复用 `internet_checksum`),
  proto==ICMP 交 **IcmpModule**(composed,不是内层 proto 表——ping 是固定 3 节点图,TCP/UDP 留 TODO)。
  `send()` 建 IPv4 头 + checksum,Ethernet 走 ARP 解下一跳,loopback 跳 L2。
- **IcmpModule**:echo-request→reply(整包 copy、type 置 0、重算 ICMP checksum、回源 IP);
  echo-reply→记录 `reply_count`/`last_id`/`last_seq`(ping 发起方观测往返)。
- **LoopbackDevice**:软件 NetDevice(无 MAC / `has_ethernet_header()=false` / 零拷贝 RX)。
  `send_l3` 只入队(8 槽 FIFO),**下轮 poll 才派发**(解重入:handler 里 send 不递归 dispatch);
  `poll_rx` 交槽指针 + `BufferSink::recycle` 归还(零拷贝 RX 证明借用通路)。

## 组合(单向依赖,无环)
`IcmpModule` ← `Ipv4Module`(`ipv4(icmp, &arp)`,ICMP TX 回调 ipv4.send);`ArpModule*` 可空(无 L2 时)。
ICMP→IPv4 的回引用是**逐调用**传 `Ipv4Module&`(handle 的形参),不存成员 → 无构造环。

## 确定性 ping 证明(kernel/test/test_net.cpp)
```
icmp.send_echo_request(lo, 127.0.0.1, id=0xABCD, seq=1)   // 建 echo req + IP 头
  → loopback 入队(28B IP+ICMP)
stack.poll()  // 一次 poll、预算制抽干:
  轮1: drain request → IPv4 → ICMP echo-request → 建 reply → loopback 入队
  轮2: drain reply   → IPv4 → ICMP echo-reply   → 记录 (count=1, id=0xABCD, seq=1)
  轮3: 队空 → break
assert icmp.reply_count()==1, last_id()==0xABCD, last_seq()==1   ✅
```
全在一个 `poll()` 里完成,无 sti/hlt、无 LAPIC timer、无 SLIRP——栈对就过,错就立刻定位层。

## 陷阱
- **MockEth 成员 `mac(EthAddr&)` 遮蔽文件作用域 `mac(...)` 助手**:类内成员 init 调 `mac(6 ints)` 撞成员签名 → 成员 `my_mac` 改直接 `EthAddr{{...}}` 构造(类外测试体不受遮蔽,`mac()` 助手照用)。
- **`arp.lookup()` 只读不插入**:误写 `arp.lookup(ip, mac(...))` 想"warm cache",实际 lookup 不改 cache,且 rvalue 绑非 const `EthAddr&` 报错 → 删之,cache 经 reply 帧填充。
- **LoopbackDevice ~12KB**(8×1518 storage):16KB 内核栈放不下 → 测试里 `static` 分配。
- **`{}` value-init 解 ODR**:DevSlot/ProtoSlot `arr[]{}` 零初始化(dev=nullptr/h=nullptr),无需默认成员初始化。
- clang-format 重排 include/对齐——以 `.clang-format` 为准。

## 验证矩阵
| gate | 结果 |
|------|------|
| host net 单测 | **5/5**(net_dispatch / net_arp_cache / net_buffer / net_checksum / **net_arp_module**) |
| run-kernel-test | **946/0**(+1 = test_net::test_ping_loopback) |
| 解耦 4 grep | 成立(kernel/net/ 零 include e1000/dma_buffer/irq;loopback 是软件设备也在栈目录) |

ArpModule 在 host mock 上证明(request/reply/cache/resolve);IPv4+ICMP 在 loopback 内核测端到端证明。

## 下一步
**L2**:E1000NetDevice adapter(copy RX,不碰 E1000Controller)+ 生产 `e1000_init` 补 `start_tx` + `cinux::net::init()` 接 main → 真 ping `10.0.2.2`。栈已在 loopback 证明,L2 失败锁死在 adapter/驱动。
