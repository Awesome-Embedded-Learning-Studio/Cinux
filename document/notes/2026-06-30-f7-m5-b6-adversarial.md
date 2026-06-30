# F7-M5 批6 — TCP 对抗性输入加固 + 负测

> F7-M5 收官后追加批（用户要求加强鲁棒性）。commit `24bd4e4`。
> 动机：批1-5 验证了「构造良好的段」逻辑对；批6 补「**坏段不崩**」——给 M6 接进生产、真流量可达时兜底。

## 背景

F7-M5 收官时确认 TcpModule 当前**不可达**（生产 net_init 不注册 TCP，进来的 TCP 包在 IPv4 L4 表查不到→静默丢），
故零崩风险。但用户问「能否加强」——把「即使将来可达也不会被坏包打崩」坐实。批6 正是这一层保险。

## 加固（代码）

`handle` 在 parse + 校验和门 + 诊断之后、FSM 之前，加**头部长度 sanity 门**：

```cpp
const uint8_t hdrlen = tcp_header_bytes(h);
if (hdrlen < sizeof(TcpHeader) || hdrlen > payload.size()) {
    return;  // malformed data offset -> drop
}
```

挡两类畸形：
- **`data_off < 5`**（声称头 < 20B）：否则 kEstablished 会 `data_len = payload.size() - 0`，把**头字节当 payload** 投递给 on_data。
- **`data_off` 声称超过实际字节**（如 data_off=15→60B 但只到 20B）：否则 data 锚点越界。

`hdrlen` 在 handle 顶部算一次、FSM 各态复用；kEstablished 原 redundant 的 `if (hdrlen > payload.size()) break;` 并入此门（删）。

## 负测（host，+6，共 15 例）

| 测试 | 验证 |
|------|------|
| 截断头（<20B） | size 门挡，valid_count 不增、不建连 |
| data_off 声称超过实际 | sanity 门挡，SYN 不建连、无 accepts |
| data_off < 5 | sanity 门挡，否则会把头字节当 payload |
| 连接表满（9th SYN） | alloc 返 nullptr→安全丢，不崩，仍 8 连 |
| 无连接的迷路 ACK | c==nullptr + 非 SYN/RST→静默丢，不回 RST |
| Established 乱序数据 | h.seq≠rcv_nxt→丢，不投递 on_data |

`data_off<5` 测是加固门的**直接验证**——没有那行 guard，这测会 fail（头字节被当 data 投递）。

## 陷阱

- **改 seg[12]（data_off）后必须重算校验和**：负测里改 data_off 后 `seg[16]=seg[17]=0; embed_tcp_checksum(...)`，
  否则校验和门先挡掉（验不出 sanity 门）。校验和门对「20 个真实字节」仍过（data_off 只是个字段值，不改实际字节）。
- **SYN flood 测用不同远端口**：8 个 SYN 各来自不同 remote（4-tuple 不同）填满表；第 9 个来自新 remote 被丢。
  SYN-ACK 堆在 dev.sent 不回投，连接停在 SynReceived（无害）。

## 验证

- host `test_net_tcp` **15/0**（+6 对抗负测）。
- `run-kernel-test-all` 两 leg 各 **988/0**（零回归——加固门只挡畸形段，现有内核测全用良好段）。

## 范围诚实

批6 只坐实**坏输入不崩 + 不产生 spurious 状态**。仍**未覆盖**（M6 接进生产后再说）：
- 真·fuzz（随机翻转字节的成百上千段）；本批是手挑的代表性畸形。
- SMP 并发（连接表无锁——可达后 net_poll kthread 与 syscall 线程并发会 race，头号 follow-up）。
- 协议级诡异（SYN|FIN 同置、重叠段、窗口探测）——当前最小实现按 flag 优先级处理，不崩但不完全合规。
