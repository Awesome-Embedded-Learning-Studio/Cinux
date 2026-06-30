# F7-M5 批2 — TCP 三次握手状态机 + 序号-ACK + RST

> F7-M5 第二批。commit `fb9f4b8`。接批1（wire + 校验和门）。

## 背景 / 目标

批1 立了 wire + 入站校验和门。批2 在其上叠**连接状态机**：连接表（4-tuple 键）、被动/主动打开、
三次握手的序号-ACK 算术、RST 拒绝。数据传输 + 四次挥手表批3。

## 设计

### 连接表

`Connection`（TCB）：state / local_port / remote_addr / remote_port / iss（我们的初始序号）/
snd_nxt（下个发序号）/ rcv_nxt（下个收序号 = 我们 ACK 的值）/ listener。固定表 `cons_[8]`，
`state==kClosed` 标空槽。4-tuple 键 = (local_port, remote_addr, remote_port)（local_addr 由设备
InDevice 钉死，不入键）。监听端口另走 `listens_[8]` 表。

### 序号-ACK 算术（核心不变量）

- **SYN 占 1 个序号、FIN 占 1 个序号、数据占 len 个序号**（批3 用）。
- `snd_nxt` = 下个要发的序号；`rcv_nxt` = 下个期望收的序号（我们 ACK 它）。
- active open：`iss=next_isn()`，发 SYN（seq=iss），`snd_nxt=iss+1`。
- 收 SYN-ACK（ack==iss+1）：`rcv_nxt=seg.seq+1`，发 ACK（seq=snd_nxt, ack=rcv_nxt），ESTABLISHED。
- 被动开（收 SYN 到监听端口）：`iss=next_isn()`，`rcv_nxt=seg.seq+1`，`snd_nxt=iss+1`，
  发 SYN-ACK（seq=iss, ack=rcv_nxt），SynReceived。
- 第 3 个 ACK（ack==iss+1）：ESTABLISHED + `on_accept`。

ISN = 确定性递增计数器（`isn_counter_` 起 0x4000，每连接 +64K，不同连接序号空间测试不重叠）。
**ISN 随机化（安全）留 follow-up**（对齐 F9 ASLR 同族）。

### TcpListener 接口

`on_accept` 纯虚（必填）；`on_data`/`on_close` 默认空（批3 数据/挥手才用，这样批2 的 listener
只覆写 on_accept，批3 再覆写另两个）。

### RST

- SYN 到未监听端口 → 回 RST|ACK（seq=0, ack=SEG.SEQ+1，RFC 793）。
- 任意状态的入站 RST → 连接置 Closed（拆连接）。

### send_segment（TX 原语）

连续缓冲区 `[伪首部 12 | TCP 头 20 | payload]` 一把 `internet_checksum`，发 TCP 部分（跳过伪首部）
经 `ipv4.send(proto=6)`——和 `UdpModule::send` 同 trick。固定 window=8192（无流控，follow-up）。

## 决策

- **handle 先校验和门再 FSM**：批1 的校验和逻辑一字不动保留（含诊断 tap），FSM 在校验通过后分派。
  批1 的 4 个测因此零改动仍绿。
- **一表跑双端（loopback test 范式）**：一个 TcpModule 同时持客户端 conn（1234→7777）和服务端 conn
  （7777→1234）。host 测 NoL2Dev 捕获 send_l3 到 `sent`，手动把 `sent[i]` 喂回 `rx` 驱动下一步——
  逐步断言每段的 flags/seq/ack，确定性验证握手时序。
- **不处理同时打开 / 纯 SYN 重传**：最小可用，无重传（明记 follow-up）。

## 陷阱

- **校验和门对 SYN-ACK 也必须过**：send_segment 算的校验和要在 handle 里经 `verify_internet_checksum`
  验过 FSM 才跑——host 测的 round-trip 自动覆盖（捕获 sent 再 deliver，过同一校验和门）。
- **NoL2Dev 的 send_l3 只捕获不回环**：reply 不会自动进 rx，测试手动喂回（与 test_net_udp 同范式）；
  真回环是批4 的 LoopbackDevice 内核测。
- **alloc 占槽用 kSynSent 占位**：caller 紧接着覆写 state（被动开→SynReceived）。单线程 poll 无窗口。

## 验证

- host `test_net_tcp`：**7/0**（+3：3-way 握手双方 ESTABLISHED + seq/ack 算术 / SYN 到关闭端口回 RST /
  重复 4-tuple 拒绝）。
- `run-kernel-test-all` 两 leg 各 **986/0**（`ALL TESTS PASSED` ×2），零回归。

## 下一步

批3：`send`（数据段 + ACK 推进 rcv_nxt）+ `close`（FIN）+ Established 状态的数据/挥手 FSM（四次挥手）。
