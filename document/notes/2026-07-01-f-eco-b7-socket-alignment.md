# F-ECO 批7:socket syscall 全对齐(accept4/setsockopt/getsockopt + getsockname/getpeername/shutdown/socketpair)

> 2026-07-01。外包 worktree `feat/outsource-f-eco-b7-socket`(从集成线 `9683e38`),cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`(`ec5ced6` 批7a + `2d2c9cc` 批7b)零冲突。两 leg **1059/0**(1055+8)+ host 69/69。**未 push**。
> busybox 试金石第七刀(`ifconfig/ping/wget/nc` 的 socket 库)。F7-M6 立了 7 个 socket syscall,本批补齐剩余 7 个 → **socket(2) 族 14 个全对齐**。applet 端到端验收留 CI build。

## 分两子批 + 并发分身(用户决策)

用户指出串行手撸子类 override 是「脱裤子放屁」,改用 [[parallel-agents-rigorous-methodology]]:
- **批7a**(handler-only,主控独占):accept4/setsockopt/getsockopt——纯 sys handler,零子类改。
- **批7b**(需子类改,3 分身并发):getsockname/getpeername/shutdown/socketpair。

### 批7a(主控)
- **共享 helper 重构**([sys_socket.cpp](../../kernel/syscall/sys_socket.cpp)):socket_from_fd + install_socket_fd 移出匿名 namespace(sys_socket.hpp 声明,前向 `cinux::net::Socket`);提取 do_accept(fd,addr,addrlen,flags)——sys_accept 退化 1 行,accept4 复用。零行为变。
- sys_setsockopt(54):**全选项 no-op accept**(无 socket-option 存储),非 socket fd→EBADF。app 设 SO_REUSEADDR 等不再 -ENOSYS(选项无效果,真语义留 options-table follow-up)。
- sys_getsockopt(55)+do_getsockopt_kernel:SOL_SOCKET 下 SO_TYPE→类型,SO_ERROR/其他→0;非 SOL_SOCKET→EOPNOTSUPP。
- sys_accept4(288):do_accept + flags;SOCK_CLOEXEC(02000000)→新 fd 设 File::cloexec(批4 字段)。

### 批7b(主控铺契约 + 3 分身)
**阶段0 主控**([socket.hpp](../../kernel/net/socket.hpp)/.cpp):+SockAddrStorage{bytes[112]}(装完整 sockaddr_in/un,family 在 byte0)+get_local/peer_addr(SockAddrStorage*) const 虚(默认 false)+do_shutdown(how)+shut_read()/shut_write()(shut_ 位)。+ 3 个不依赖子类的 handler(getsockname/getpeername/shutdown)+号/dispatch/CMake/测试骨架。
**阶段1 三分身零文件交叉**(并发,主控合并一次编绿):
- 分身 Unix:unix_socket.{hpp,cpp} override get_local/peer_addr + pair_with(socketpair 对端互连)+ send/recv shutdown 检查;+ sys_socketpair.{hpp,cpp}(AF_UNIX STREAM 对)。
- 分身 Udp:udp_socket.{hpp,cpp} override get_local/peer_addr + send/sendto/recv shutdown 检查。
- 分身 Tcp:tcp_socket.{hpp,cpp} override get_local/peer_addr + send/recv shutdown 检查。
- sys_getsockname(51)/getpeername(52):填 SockAddrStorage → 按 domain() 拷 sizeof(sockaddr_in/un) 到 user;未命名/未连→EOPNOTSUPP。
- sys_shutdown(48):do_shutdown(how);how>2→EINVAL。SHUT_WR→send BrokenPipe、SHUT_RD→recv EOF(子类入口检查)。

## 机制测试(防假绿,8 测)
- 批7a:setsockopt no-op+badfd / getsockopt SO_TYPE(STREAM=1/DGRAM=2)+ bad-level→EOPNOTSUPP/bad-fd / **accept4 SOCK_CLOEXEC round-trip**(经 install_socket_fd 装 UnixSocket 成 fd,accept4 取 child 验 cloexec=true;无 flag 验 false)。
- 批7b:getsockname(Unix bound→sockaddr_un path 精确匹配)+ getpeer_addr(pair_with 后有名/未连无名)+ shutdown(SHUT_WR→BrokenPipe / SHUT_RD→EOF)+ **socketpair round-trip**(do_socketpair_kernel→两 fd 经 InodeOps 内核缓冲写字节往返)。

## 教训留存:并发分身对的地方
批7b 三个子类 override(Unix/Udp/Tcp 各自 .hpp/.cpp)天然零文件交叉——正是 [[parallel-agents-rigorous-methodology]] 的理想场景。主控先铺好基类契约(socket.hpp 虚+SockAddrStorage+shut_),三分身并发填(各 ~40 行),主控合并**一次编绿 + 两 leg 1059/0**。比串行手撸省时且每个分身 prompt 注入完整契约 + 严格单文件边界 → 零合并冲突。**启示:识别出零文件交叉的并行面就并发,别手撸**。

## follow-up(留后续)
- **socket-option 存储**:setsockopt 现全 no-op;SO_REUSEADDR/RCVBUF/KEEPALIVE 等真语义需 options table + 各层消费。
- **SOCK_NONBLOCK**(accept4/socket 的 type 标志):接受不落地(无 per-fd nonblock 标志,follow-up 接批4 F_SETFL O_NONBLOCK)。
- **shutdown 传播对端**:现只记本端 shut_;不向对端发 FIN / peer-EOF(真 TCP FIN 留 TCP 层)。
- **socketpair SOCK_DGRAM / AF_UNIX DGRAM**:现只 STREAM 对;DGRAM 对 + sendto(path) 留后续。
- **getpeername AF_UNIX 具名对端**:peer 未 bind 时返匿名(Linux 行为);具名对端需 peer 存其 bind path。
- busybox `nc`/`wget` applet 端到端验收:留 CI。

## 验证
`run-kernel-test-all` 两 leg:单核 1059/0 → -smp 2 1059/0 → ALL TESTS PASSED(基线 1055 + 8 socket 对齐)。`test_host` 69/69(socket.hpp 公共头 +SockAddrStorage/虚/shut_,host mock 零回归)。`check_net_decoupling` 绿。

**push/PR 归用户**——F-ECO 外包线,等回主线。
