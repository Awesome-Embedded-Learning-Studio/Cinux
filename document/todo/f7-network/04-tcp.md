# M5: TCP 协议

> 可靠连接传输层。三次握手、四次挥手、拥塞控制。
> 复杂度最高的网络层。

> **F7-M5 范围栅栏（2026-06-30 立项，worktree-f7-m5-tcp）**：本里程碑只做 **T1 状态机（握手/挥手）+ T2 段处理（SYN/ACK/FIN/RST/data）+ 伪首部校验和**，最小可用（单方向数据、**无重传**）。**T3 滑动窗口/超时重传/RTO、T4 拥塞控制** 留 follow-up（需内核 timer-wake，CinuxOS 现无）；**T5 TCP Socket（listen/accept/recv）** 进 F7-M6（socket 层）。详见 PLAN「🔄 F7-M5 TCP」段。

## 任务清单

### T1: TCP 状态机

```
CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT
                                              → CLOSE_WAIT → LAST_ACK → CLOSED
LISTEN → SYN_RCVD → ESTABLISHED
```

- [x] TcpState enum + 状态转换表（F7-M5：Closed/SynSent/SynReceived/Established/FinWait1/FinWait2/CloseWait/LastAck；TIME_WAIT 略——无 timer）
- [x] TCP 控制块 (TCB) 结构体（F7-M5：`Connection` —— state/local/remote/iss/snd_nxt/rcv_nxt/listener）

### T2: TCP 段处理

```cpp
struct TCPHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t data_offset_flags;  // SYN/ACK/FIN/RST
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed));
```

- [x] SYN 处理（新建连接）（F7-M5：被动开 SYN-ACK / 主动开 connect）
- [x] ACK 处理（确认收到的数据）（F7-M5：握手第 3 ACK + 数据 ACK 推进 rcv_nxt）
- [x] FIN 处理（关闭连接）（F7-M5：四次挥手 FinWait1/2/CloseWait/LastAck）
- [x] RST 处理（异常终止）（F7-M5：SYN 到未监听端口回 RST + 入站 RST 拆连接）
- [x] 数据段收发（F7-M5：send/close，严格按序，无重传）

### T3: 滑动窗口

- [ ] 发送窗口管理
- [ ] 接收窗口通告
- [ ] 超时重传（RTO 计算）
- [ ] 序列号正确性检查

### T4: 拥塞控制（简化版）

- [ ] 慢启动（cwnd 从 1 MSS 开始指数增长）
- [ ] 拥塞避免（线性增长）
- [ ] 快速重传（3 个重复 ACK）
- [ ] 不做快速恢复（简化）

### T5: TCP Socket

- [ ] listen() — 标记为被动打开
- [ ] accept() — 从完成队列取连接
- [ ] connect() — 主动三次握手
- [ ] send() / recv() — 流式数据传输
- [ ] close() — 四次挥手
