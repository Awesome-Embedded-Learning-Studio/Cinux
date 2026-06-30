# F7-M5 批1 — TCP wire 层 + 伪首部校验和门

> 里程碑 F7-M5 TCP 第一批。接 F7-M4 UDP ✅。worktree `worktree-f7-m5-tcp`（从干净 main `c0188cd`）。
> commit `30a136a`。范围栅栏 + 全批次设计见 PLAN「🔄 F7-M5 TCP」段。

## 背景

F7-M4 把 IPv4 的 L4 分派做成单一机制（`L4Handler` 缝 + `Ipv4Module` 内部 proto→handler 表，ICMP 已迁入）。
加 TCP 因此是「再注册一个 L4Handler」——`ipv4.add_l4(kIpProtoTcp, tcp)`，不碰分派机制本身。
本里程碑范围（用户拍板）：状态机（握手/序号-ACK/挥手）+ 伪首部校验和，**最小可用无重传**；
重传/窗口/拥塞/Socket 留 follow-up / F7-M6。

批1 只立 wire 层 + 入站校验和门：确定 TcpHeader 的线上格式正确、校验和能算能验、proto 6 经 L4 表
派发到 TcpModule。FSM（握手/数据/挥手）在批 2/3 叠加。

## 目标

- `TcpHeader` 20B 线布局（parse/build，host-order 视图，与 UDP/ICMP 同范式）。
- `kIpProtoTcp=6` 常量进 ipv4.hpp（不改 `add_l8`）。
- `TcpModule::handle` 入站校验和门：伪首部校验和（proto 6）验过才记录；坏校验和静默丢。
- host 单测 `test_net_tcp`：头 round-trip / flags 线序 / 校验和 round-trip / 坏校验和 drop / proto 6 派发。

## 设计与决策

### wire 格式

`TcpHeader` 字段 host-order：`src_port/dst_port`（16）、`seq/ack`（32）、`data_off`（4 位，头长 32 位字数）、
`flags`（byte 13，低 6 位 FIN/SYN/RST/PSH/ACK/URG）、`window/checksum/urgent_ptr`（16）。`static_assert(sizeof==20)`。

线上 byte 12 = `(data_off << 4)`（保留低位清零），byte 13 = flags；seq/ack 32 位大端。`parse_tcp`/`build_tcp_header`
逐字节拼（与 `parse_udp`/`build_udp_header` 同风格，避免结构体 overlay 的端序坑）。`tcp_header_bytes(h)=data_off*4`。

### 校验和：连续缓冲区法（沿用 UDP）

TCP 与 UDP 同用 12 字节伪首部（src IP / dst IP / 0 / proto / L4 长度），proto=6。`handle` 收到的 `payload` 就是
整个 TCP 段（IPv4 已剥头），故 `payload.size()` 即 TCP 段长。重建 `[伪首部 12 | 段]` 一把
`verify_internet_checksum`——与 `UdpModule::handle` 同一把算法（udp.cpp 的 `build_pseudo_header` 镜像，仅 proto 字节不同）。

**关键差异（与 UDP）**：TCP 校验和**必填**。UDP 的 checksum=0 是「无校验和」可跳过（RFC 768）；TCP 没这个口子，
0 是协议违规。故 `handle` **总是**校验，不判 checksum=0 跳过。

### handle 诊断 tap

批1 的 handle 验完校验和后把段记进诊断字段（`valid_count_` / `last_seq_` / `last_flags_` 等）——这是校验和门
的可观测面，让 host 测能断言「验过/丢掉」。这批诊断在批2 会被 FSM 取代为主路径，但作为轻量 tap 保留（同
`IcmpModule::reply_count_` 范式）。

### 骨架范围

批1 的 `tcp.cpp` 只有 `build_pseudo_header` + `handle`（校验和门）。**没有** TX（`send_segment`）、连接表、
`listen`/`connect`/`send`/`close`——这些是批 2/3。`tcp.cpp` 编进 kernel（CMake 已加），暂无 caller，无害。

## 陷阱

- **基线是 986/0 不是 969/0**：main 已在 `c0188cd` 合入 F10-M3 PTY（PR#50），内核测数到 986。F7-M4 的 969 是旧值。
  批1 不加内核测，故两 leg 仍 986/0（零回归）。
- **并发会话占 VNC :0**：本机同时有 `worktree-f8-pipe-fifo` / `worktree-f6-m2-procfs` 等会话跑 gate，全用硬编码
  `-vnc :0` 互撞（qemu.cmake 里 QEMU_DISPLAY 写死 `:0`，无 env 覆盖）。验证时给本 worktree 临时切 `-vnc :5`（5905 空闲），
  跑完 `git checkout -- cmake/qemu.cmake` 还原（不入 diff）。这是验证 hack，非代码改动。
- **CINUX_BUILD_TESTS 默认 OFF**：该 CMake 选项无 `option()` 声明，默认未定义→false。host 单测必须
  `-DCINUX_BUILD_TESTS=ON` 才编（F7-M4 同款 GOTCHA）。
- **worktree 子模块未初始化**：新 worktree 的 `third_party/Cinux-Base` / `Cinux-GUI` 未签出，本机连不上 github，
  从主仓库 rsync 子模块树（排除 `.git`）即可（构建只需源文件）。

## 验证

- host `test_net_tcp`：**4/0**（头 round-trip / 校验和 round-trip 记录 / 坏校验和 drop / proto 6 派发）。
- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`（本地 smoke OFF + tests ON + 临时 `-vnc :5`）：
  **两 leg 各 986 passed, 0 failed**（`[TEST] ALL TESTS PASSED (exit code 0)`），SMP leg AP-wake readback PASS。零回归。

## 下一步

批2：连接表（4-tuple 键）+ `listen`/`connect` + 握手 FSM（SYN→SYN-ACK→ACK，序号-ACK 算术）+ RST。
