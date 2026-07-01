# F8-M3:AF_UNIX socket(内存命名空间 + 字节流 round-trip)

> 2026-07-01。分支 `feat/outsource-f8-unix-socket`(worktree `.claude/worktrees/wt-f8-unix`,从集成线 `feat/f-eco-b2-vfs-syscalls` `47573bc`)。F8 IPC 第三里程碑:AF_UNIX socket。
> 收进 F7-M6 的 **Socket=InodeOps 适配器**——socket fd 仍是 `File→Inode→SocketOps`,`sys_read/write/close` 零改;AF_UNIX 复用 `socket_ops()` 单例 + `install_socket_fd`。
>
> **打法(单线程主控,非分身)**:范围自洽(AF_UNIX 无 NIC/L4 模块,不与批2/ext2 交叉),一个 feat commit + 一个 docs commit 收。先读全签名模式(socket/tcp_socket/fifo/sys_socket/test_socket)再写。

## 架构(走 Socket base 虚派发,内存命名空间不依赖 fs)

- **Socket base 加 path 虚函数** `bind_path(const char*)` / `connect_path(const char*)`(默认 NotImplemented)。原因:path 塞不进 AF_INET 形状的 `bind(uint16_t)`/`connect(Ipv4Addr,port)`;改既有签名 blast radius 大(动 Udp/TcpSocket,已合 main)。加法非破坏(Udp/TcpSocket 零改),对齐既有 ioctl/open/chmod default-virtual 范式。`listen/accept/send/recv` 真正复用现有 virtuals(family-agnostic)。
- **UnixRegistry**([unix_socket.cpp](../../kernel/net/unix_socket.cpp)):内存 path→listening socket 固定表 + Spinlock,仿 [FifoRegistry](../../kernel/ipc/fifo.cpp)。**不依赖 tmpfs/ext2 真路径**(hobby-OS 简化,真 fs-backed bind() 留 follow-up)。
- **UnixSocket 两种角色一个类**(mirror TcpSocket):
  - Listening(bind_path 注册 registry;listen 翻标志;accept 出队 connected child)。
  - Connected(connect 后 client,或被 accept 的 child):持 peer 指针;`send` 写 **peer** 的 4KB RX 环,`recv` 排干自己的。
- **connect_path 建连**:lookup registry→server;new child C;互连 `client.peer_=C`/`C.peer_=client`;C 入 server.accept 队列。**立即建立,无内核握手**——确定性单线程测试可 send-before-accept(字节缓冲在 C.rx_)。
- **阻塞**:`prepare_to_wait/schedule_blocked/wake_one`(抄 tcp_socket.cpp/pipe),host-guarded(`CINUX_HOST_TEST`→WouldBlock)。**不 sti/hlt**(避 sti-in-syscall→#DF,memory sys-ping-df-sti-in-syscall)。
- **sys_socket 收 AF_UNIX 直接 `new UnixSocket`**(自洽无栈依赖):不走 `create_socket`——那是"接生产 L4 栈"的唯一桥,test kernel 无 net_init 时返 nullptr。直接在 sys_socket.cpp 处理避开改 `net_init.cpp`+`net_stub.cpp` 两处(CINUX_NET gate 两边)。
- **sys_bind/connect/accept 按 `Socket::domain()` 派发** AF_UNIX→path 虚函数 + `parse_sockaddr_un`;listen/sendto/recvfrom 无需改(STREAM connected 走 addr=0→send/recv)。

## 批表

| 批 | 范围 | Commit | 测试 |
|----|------|--------|------|
| 1 | Socket base +path 虚 + UnixSocket/UnixRegistry + sys_socket/bind/connect/accept AF_UNIX + CMake + 4 机制测试 | `7552ca2` | 两 leg 1037/0(1033+4)+ check_net_decoupling 绿 |

## GOTCHA(机制测试第一轮抓的——copy_from_user 范围检查拒内核地址)

- **第一轮 3 测红**:echo/connect_unbound/bind_duplicate 全挂(`sys_bind(...) == 0` 不成立)。根因不在 UnixSocket,在 **测试侧**:第一版 echo + 负测走**syscall 层**(栈上 sockaddr_un 传 sys_bind),而 `copy_from_user` 有 `is_user_vaddr` 纯范围检查([user_access.hpp](../../kernel/arch/x86_64/user_access.hpp) bit47=0 才是 user)——**ring0 测试内核的栈地址是内核态,被拒→-EFAULT**。
- 这正是 **AF_INET TCP/UDP echo 测走 Socket 方法直接调、不经 sys_bind** 的原因。改 echo + 两负测走 UnixSocket **直接方法**(bind_path/connect_path/send/recv),`returns_fd` 仍走 sys_socket(无 user ptr,过——证 sys_socket 创 UnixSocket)。
- **教训**:`copy_from_user` 不是"任意地址都能拷"——它强制 user-only(安全门,防内核误用 accessor 拷内核→内核)。测试内核要验 syscall 的 user-ptr 路径,得真建 user 地址空间(如 ring-3 smoke),不能塞内核栈地址。sys_bind 的 sockaddr_un 解析留生产 musl 覆盖(简单 copy + family 检查 + NUL 终止)。

## 防假绿(语义精确匹配,非退出码 0)

机制测试 4 个,F8-M3 AF_UNIX 段:
1. `returns_fd`:sys_socket(AF_UNIX,STREAM)→fd≥0;inode->ops==socket_ops;fs_private domain()==AF_UNIX/type()==STREAM。
2. `loopback_echo`:server bind_path+listen,client connect_path,client send("unix"),server accept→child recv **逐字节精确匹配**,回程 child send→client recv **逐字节精确匹配**。
3. `connect_unbound`:connect_path("/unix_nobody")→!ok 且 error==NotFound(ENOENT)。
4. `bind_duplicate`:两 socket 同 bind_path→第二个 !ok 且 error==AlreadyExists(EEXIST)。

## follow-up(留后续)

- **fs-backed bind()**:现 path 在内存 registry,不创建/不查 fs 真节点(对齐 Linux 需 tmpfs 上真 socket inode)。hobby-OS 简化,需接 DevFS/tmpfs dynamic node。
- **abstract socket**(path 首字节 `\0`):现 parse_sockaddr_un 拒空 path;Linux abstract namespace 不占 fs。
- **socketpair / AF_UNIX DGRAM sendto(path)**:本里程碑只 STREAM 的 bind/connect/listen/accept/send/recv;DGRAM connectionless sendto(path) + socketpair 留后续。
- **send-side flow control**:ring 满返 WouldBlock(EAGAIN),非阻塞(真阻塞 send 需 send-wait queue + peer drain 唤醒)。单线程测试 4KB 环 + 4 字节消息不触发。
- **SMP peer 可见性**:peer_ 写一次(connect/accept 设),send 快照后只持 peer 锁;registry 锁不与 socket 锁嵌套(无 AB-BA)。但 peer_ 跨 CPU 写→读的 happens-before 无单一锁串起(SMP rigor 留后续,单线程测试不触发)。
- **connect 阻塞到 accept**:现 connect 立即建立(不入队等 accept),真 Linux STREAM connect 阻塞到 server accept(需 read/write 两端就绪语义)。
- **close 无 InodeOps::release 钩子**:sys_close 只 free File,不调 Socket::close()→listener 不从 registry 撤名(pipe/socket 共同 hobby 限制);测试用唯一 path 避碰撞。

## 验证

`timeout 200 cmake --build build --target run-kernel-test-all` 两 leg:单核(cpu_count=1)1037/0 → -smp 2(cpu_count=2)1037/0 → ALL TESTS PASSED。基线 1033/0 + 4 AF_UNIX 测。`check_net_decoupling` 绿(unix_socket.cpp 只 include socket.hpp/ring_buffer/proc/sync + host-guarded scheduler,无 e1000/dma/irq)。

**push/PR 归用户**——这是 F-ECO 外包线 worktree,等回集成线 `feat/f-eco-b2-vfs-syscalls` cherry-pick + 修冲突 + 验证。
