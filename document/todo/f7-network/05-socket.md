# M6: Socket API（BSD socket 系统调用）

> 用户态网络编程接口。TCP/UDP 协议层（M4/M5）之上的 socket 适配层 + socket 系统调用。
> 本里程碑让 CinuxOS 网络走到「可用」：一个 musl 用户程序能用 socket()/connect()/send()/recv() 收发数据。

> **F7-M6 范围栅栏（2026-06-30 立项，worktree-f7-m6-socket，从干净 main `f1f29aa`）**：socket fd 走 **InodeOps 子类**（对齐 PTY/pipe，socket fd → File → Inode → SocketOps，`sys_read/write/ioctl/close` 零改动）。TcpModule/UdpModule **保持纯协议层不动**，per-socket RX 环 + 阻塞 + accept 队列放进 Socket 适配器，在 listener 缝（`UdpListener::on_udp` / `TcpListener::on_accept/on_data/on_close`）上挂——回调里**拷贝借来的帧**进环，send 直接调 module。阻塞用现成 `prepare_to_wait/schedule_blocked/unblock`（F8 pipe 那套），从 `net_poll` kthread 上下文唤醒，**不需 timer**。生产 `net_init.cpp` 注册 TCP/UDP（`add_l4`）+ 挂 LoopbackDevice + dev 路由选择。
>
> **明确不做（deferred）**：① TCP 重传/RTO/窗口/拥塞（TcpModule 一个没有，需内核 timer=HPET 周期中断=F5-M4 follow-up，硬前置非 M6 活）② TIME_WAIT/ISN 随机化/乱序重组 ③ IPv6/AF_UNIX（F8）/sendmsg cmsg ④ epoll/select（F8）/第二 NIC/中断驱动 RX（仍 net_poll 轮询）。→ **最小可用 = loopback 端到端干通 + SLIRP 尽力**（loopback 零丢包；SLIRP TCP 无重传可能丢）。

## 架构（4-agent 设计面板核实，已读码）

```
sys_socket/bind/connect/listen/accept/sendto/recvfrom/close   ← kernel/syscall/sys_socket.cpp
        │  fd → File → inode → SocketOps（InodeOps 子类）        ← kernel/net/socket_ops.{hpp,cpp}
        ▼
   Socket base（端点/proto/per-socket RX 环/等待队列/virtuals）  ← kernel/net/socket.{hpp,cpp}（纯逻辑，host 可测）
   ├─ UdpSocket : Socket, UdpListener   ← udp_socket.*（on_udp→拷贝进环；send→UdpModule::send）
   └─ TcpSocket : Socket, TcpListener   ← tcp_socket.*（on_accept 队列/on_data→环；listen/accept/connect/send→TcpModule）
        │  listener 缝：模块收包 → 回调 → 拷贝进 socket 环 → 唤醒阻塞 recv/accept
        ▼
   UdpModule / TcpModule（纯协议层，M4/M5 已就位，不动）
        │  production: net_init.cpp 实例化 + add_l4(kIpProtoUdp/...) + add_l4(kIpProtoTcp/...)
        ▼
   Ipv4Module L4 表 → NetStack → e1000/SLIRP + LoopbackDevice（net_poll kthread 驱动 RX）
```

**关键点**：
- **Socket = InodeOps 子类**，照抄 PTY/pipe 范式。`socket()` 装合成 Inode（抄 `sys_pipe` unique_ptr→release），`accept()` 抄 `PtmxOps::open` cloning。`sys_read/write/ioctl/close` 对 socket fd 与 pipe fd 一视同仁，零改动。
- **不碰 FSM**：协议层 listener 缝正是 M4/M5 当年给 M6 埋的。适配器在缝上挂回调，`on_udp`/`on_data` **拷贝借来的帧**（设备 dispatch 后回收 buffer）进 per-socket 环。
- **阻塞现在就能做**：`prepare_to_wait/schedule_blocked`（F8 pipe），从 `net_poll` kthread 的 listener 回调唤醒。#DF 安全（syscall 内不 sti/hlt）。
- **解耦门** `check_net_decoupling.sh`：`kernel/net/` 不能 include driver/dma/irq。Socket 抽象放 `kernel/net/`（只用 net 模块 + scheduler 等待队列，合法）；实例化 + `add_l4` + accessor 放 `net_init.cpp`（drivers/net/，唯一看到双方处）。

## 批表（B0-B4）

| 批 | 范围 | 并行 |
|----|------|------|
| 0 | 立项 docs（本段）+ ROADMAP/PLAN | trunk |
| 1 | 底座：生产注册（g_udp/g_tcp+add_l4+挂 loopback+dev 路由 accessor）+ Socket base 纯逻辑 + SocketOps 缝 + socket syscall 通用派发（via Socket virtuals）+ syscall 号注册。内部可拆 B1a(net_init+base)/B1b(fd+syscall) | 顺序 |
| 2 | UDP socket：UdpSocket 适配器 + bind/sendto/recvfrom + 阻塞 + host 单测 + loopback echo | 本地子分支 ‖ |
| 3 | TCP socket：TcpSocket 适配器 + accept 队列 + per-连接环 + listen/accept/connect/send/recv + 阻塞 + remote→socket 路由 + host 单测 + loopback echo | 本地子分支 ‖ |
| 4 | 端到端 demo（loopback 内核 echo 回归门 + musl 静态 socket 程序）+ 收官 | trunk |

## 任务清单

### T1: Socket 抽象层（InodeOps 适配器）

```cpp
class Socket {  // kernel/net/socket.hpp —— 纯逻辑，host 可测
public:
    virtual ~Socket() = default;
    virtual ErrorOr<void>     bind(uint16_t local_port) = 0;
    virtual ErrorOr<void>     connect(Ipv4Addr remote, uint16_t remote_port) = 0;  // TCP
    virtual ErrorOr<void>     listen() = 0;                                         // TCP
    virtual ErrorOr<Socket*>  accept() = 0;                                         // TCP（返新建子 Socket）
    virtual ErrorOr<int64_t>  send(const uint8_t* buf, uint32_t len) = 0;
    virtual ErrorOr<int64_t>  recv(uint8_t* buf, uint32_t len) = 0;                // 阻塞 deque 环
    /* ...sendto/recvfrom 带 addr 重载（UDP）... */
};
class SocketOps  : public cinux::fs::InodeOps { /* read=recv/write=send/ioctl/stat stub */ };
class UdpSocket  : public Socket, public UdpListener { /* on_udp→拷贝进环 */ };
class TcpSocket  : public Socket, public TcpListener { /* on_accept 队列/on_data→环/on_close */ };
```

- [ ] Socket base（端点/proto/per-socket RX 环/等待队列/virtuals，纯逻辑 host 单测）
- [ ] SocketOps InodeOps 缝（read/write/ioctl/stat，`fs_private` 存 Socket*）
- [ ] UdpSocket（on_udp→环、bind/sendto/recvfrom、阻塞）
- [ ] TcpSocket（accept 队列、on_data→per-连接环、listen/accept/connect/send/recv、阻塞、remote→socket 路由）

### T2: Socket 系统调用（Linux x86_64 号；syscall_nums.hpp 41-55/288 全空已核实无撞号）

| Syscall | 编号 | 说明 |
|---------|------|------|
| sys_socket | 41 | 创建 socket（AF_INET/SOCK_STREAM\|SOCK_DGRAM）→ fd |
| sys_connect | 42 | 主动连接（TCP）/ 设对端（UDP） |
| sys_accept | 43 | 取已完成连接（阻塞） |
| sys_sendto | 44 | 发数据（UDP）/ send |
| sys_recvfrom | 45 | 收数据（阻塞） |
| sys_bind | 49 | 绑定本地端口 |
| sys_listen | 50 | 被动监听（TCP） |
| sys_setsockopt | 54 | 存根（SO_REUSEADDR，余 ENOPROTOOPT） |
| sys_accept4 | 288 | accept + flags（对齐 musl） |

- [ ] sys_socket/bind/connect/listen/accept/sendto/recvfrom/close 通用派发（fd→File→inode→SocketOps→Socket virtual）
- [ ] sockaddr_in 打包/拆包（copy_to/from_user SMAP 安全，抄 sys_read 暂存范式）
- [ ] syscall 号注册（syscall_nums.hpp + syscall.cpp register_builtin_handlers）
- [ ] sys_close 复用 FDTable::close，SocketOps 析构拆连接（TCP FIN）

### T3: 生产接线 + 端到端验证

- [ ] net_init.cpp 注册 UdpModule/TcpModule（`add_l4`）+ 挂 LoopbackDevice + dev 路由 accessor
- [ ] 内核 loopback TCP/UDP echo 回归测（确定性，socket→module→loopback→module→socket 全链）
- [ ] musl 静态 socket 程序（socket/connect/send/recv 真用户态，SLIRP/loopback）
- [ ] host ASAN（per-open 资源释放，无 release 钩子则测试自释放，参 F8 test_fifo 教训）
