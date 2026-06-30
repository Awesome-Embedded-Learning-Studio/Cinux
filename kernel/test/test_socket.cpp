/**
 * @file kernel/test/test_socket.cpp
 * @brief In-kernel tests for the F7-M6 socket syscall plumbing (batch 1b).
 *
 * B1b ships the fd + syscall machinery with a STUB Socket: socket()/close()
 * work, and bind/connect/listen/accept/sendto/recvfrom return -ENOSYS until B2
 * (UdpSocket) / B3 (TcpSocket) override the virtuals.  These tests prove the
 * plumbing -- socket() allocates a real fd backed by a SocketOps inode, arg
 * validation rejects a bad family/type, the fd resolves back to the Socket, and
 * the stub send() is reached.  Real send/recv + loopback echo land with B2/B3.
 *
 * The stub Socket needs no net stack, so this runs in the test kernel (which
 * does not bring up production net_init) -- the create_socket() factory returns
 * a bare Socket for AF_INET regardless of g_ready in B1b.
 */

#include <stdint.h>

#include "kernel/errno.hpp"               // kEafnosupport / kEprotonosupport
#include "kernel/fs/file.hpp"             // File
#include "kernel/fs/vfs_mount.hpp"        // current_fd_table()
#include "kernel/net/arp.hpp"             // ArpModule (echo stack)
#include "kernel/net/icmp.hpp"            // IcmpModule
#include "kernel/net/ipv4.hpp"            // Ipv4Module / kIpProto*
#include "kernel/net/loopback_device.hpp" // LoopbackDevice
#include "kernel/net/net_stack.hpp"       // NetStack / InDevice
#include "kernel/net/socket.hpp"          // Socket / socket_ops / kAfInet / kSock*
#include "kernel/net/tcp.hpp"             // TcpModule
#include "kernel/net/tcp_socket.hpp"      // TcpSocket
#include "kernel/net/udp.hpp"             // UdpModule
#include "kernel/net/udp_socket.hpp"      // UdpSocket
#include "kernel/syscall/sys_close.hpp"   // sys_close
#include "kernel/syscall/sys_socket.hpp"  // sys_socket
#include "kernel/test/big_kernel_test.h"

using cinux::fs::current_fd_table;
using cinux::net::ArpModule;
using cinux::net::IcmpModule;
using cinux::net::InDevice;
using cinux::net::Ipv4Addr;
using cinux::net::Ipv4Module;
using cinux::net::LoopbackDevice;
using cinux::net::NetDevice;
using cinux::net::NetStack;
using cinux::net::Socket;
using cinux::net::TcpModule;
using cinux::net::TcpSocket;
using cinux::net::UdpModule;
using cinux::net::UdpSocket;
using cinux::net::kAfInet;
using cinux::net::kEtherTypeArp;
using cinux::net::kEtherTypeIpv4;
using cinux::net::kIpProtoTcp;
using cinux::net::kIpProtoUdp;
using cinux::net::kLoopbackAddr;
using cinux::net::kSockDgram;
using cinux::net::kSockStream;
using cinux::net::socket_ops;
using cinux::syscall::sys_close;
using cinux::syscall::sys_socket;

namespace test_socket {

static constexpr uint64_t kFiller = 0;

void test_socket_dgram_returns_fd() {
    int64_t fd = sys_socket(kAfInet, kSockDgram, 0, kFiller, kFiller, kFiller);
    TEST_ASSERT_GE(fd, 0);
}

void test_socket_stream_returns_fd() {
    int64_t fd = sys_socket(kAfInet, kSockStream, 0, kFiller, kFiller, kFiller);
    TEST_ASSERT_GE(fd, 0);
}

void test_socket_rejects_bad_family() {
    // AF_UNIX (1) -- only AF_INET is supported.
    int64_t r = sys_socket(1, kSockDgram, 0, kFiller, kFiller, kFiller);
    TEST_ASSERT_EQ(r, -cinux::kEafnosupport);
}

void test_socket_rejects_bad_type() {
    // SOCK_RAW (3) -- only STREAM / DGRAM are supported.
    int64_t r = sys_socket(kAfInet, 3, 0, kFiller, kFiller, kFiller);
    TEST_ASSERT_EQ(r, -cinux::kEprotonosupport);
}

void test_socket_fd_routes_to_socket_stub() {
    int64_t fd = sys_socket(kAfInet, kSockDgram, 0, kFiller, kFiller, kFiller);
    TEST_ASSERT_GE(fd, 0);

    cinux::fs::File* f = current_fd_table().get(static_cast<int>(fd));
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_NOT_NULL(f->inode);
    // The fd's inode ops IS the shared SocketOps -> it is a socket fd.
    TEST_ASSERT_EQ(f->inode->ops, &socket_ops());

    auto* s = static_cast<Socket*>(f->inode->fs_private);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQ(s->type(), kSockDgram);

    // Dispatch reaches the Socket: send on an unconnected/unconfigured socket
    // fails (UdpSocket -> InvalidArgument; a bare stub -> NotImplemented).  The
    // exact errno is protocol-specific; here we only assert the fd routes to a
    // live Socket that answers.
    uint8_t b = 0;
    auto    r = s->send(&b, 0);
    TEST_ASSERT_FALSE(r.ok());

    // close() frees the File (Socket/Inode share the pipe-style hobby leak).
    TEST_ASSERT_EQ(sys_close(static_cast<uint64_t>(fd), kFiller, kFiller, kFiller, kFiller, kFiller),
                   0);
}

// --- B2: UdpSocket end-to-end over a deterministic loopback stack ---
namespace {
// echo_route is a function pointer (UdpSocket::DevRoute) so it cannot capture;
// the loopback device it returns lives at file scope.
LoopbackDevice echo_lo;
NetDevice&     echo_route(Ipv4Addr /*dst*/) {
    return echo_lo;
}
// A second loopback for the TCP echo test (a NetDevice is attached to one stack).
LoopbackDevice tcp_echo_lo;
NetDevice&     tcp_echo_route(Ipv4Addr /*dst*/) {
    return tcp_echo_lo;
}
}  // namespace

void test_udp_socket_loopback_echo() {
    // Function-local statics mirror test_net::test_udp_loopback: the modules +
    // LoopbackDevice are too large for the 16 KB kernel stack.
    static ArpModule  arp;
    static IcmpModule icmp;
    static Ipv4Module ipv4(icmp, &arp);
    static UdpModule  udp;
    static NetStack   stack;
    static bool       init = false;
    if (!init) {
        stack.add_protocol(kEtherTypeArp, arp);
        stack.add_protocol(kEtherTypeIpv4, ipv4);
        ipv4.add_l4(kIpProtoUdp, udp);
        InDevice cfg{};
        cfg.local   = kLoopbackAddr;  // 127.0.0.1
        cfg.gateway = kLoopbackAddr;
        stack.attach(echo_lo, cfg);
        init = true;
    }

    UdpSocket server(udp, ipv4, stack, echo_route);
    UdpSocket client(udp, ipv4, stack, echo_route);
    TEST_ASSERT_TRUE(server.bind(7777).ok());
    TEST_ASSERT_TRUE(client.bind(1234).ok());

    // client -> server (127.0.0.1:7777). One poll drains the loopback round-trip:
    // send enqueues the IP packet, poll dispatches it -> UdpModule -> server.on_udp.
    static const uint8_t msg[] = {'e', 'c', 'h', 'o'};
    auto                 sr    = client.sendto(kLoopbackAddr, 7777, msg, sizeof(msg));
    TEST_ASSERT_TRUE(sr.ok());
    stack.poll();

    uint8_t  rbuf[8] = {};
    Ipv4Addr src{};
    uint16_t sport = 0;
    auto     rr    = server.recv(rbuf, sizeof(rbuf), &src, &sport);
    TEST_ASSERT_TRUE(rr.ok());
    TEST_ASSERT_EQ(*rr, static_cast<int64_t>(sizeof(msg)));
    TEST_ASSERT_EQ(sport, 1234u);  // client's bound source port

    // echo back: server -> client (127.0.0.1:1234).
    auto er = server.sendto(kLoopbackAddr, 1234, rbuf, static_cast<uint32_t>(sizeof(msg)));
    TEST_ASSERT_TRUE(er.ok());
    stack.poll();
    uint8_t cbuf[8] = {};
    auto    cr      = client.recv(cbuf, sizeof(cbuf), nullptr, nullptr);
    TEST_ASSERT_TRUE(cr.ok());
    TEST_ASSERT_EQ(*cr, static_cast<int64_t>(sizeof(msg)));
}

// --- B3: TcpSocket end-to-end over a deterministic loopback stack ---
void test_tcp_socket_loopback_echo() {
    static ArpModule  arp;
    static IcmpModule icmp;
    static Ipv4Module ipv4(icmp, &arp);
    static TcpModule  tcp;
    static NetStack   stack;
    static bool       init = false;
    if (!init) {
        stack.add_protocol(kEtherTypeArp, arp);
        stack.add_protocol(kEtherTypeIpv4, ipv4);
        ipv4.add_l4(kIpProtoTcp, tcp);
        InDevice cfg{};
        cfg.local   = kLoopbackAddr;
        cfg.gateway = kLoopbackAddr;
        stack.attach(tcp_echo_lo, cfg);
        init = true;
    }

    TcpSocket server(tcp, ipv4, stack, tcp_echo_route);
    TcpSocket client(tcp, ipv4, stack, tcp_echo_route);
    TEST_ASSERT_TRUE(server.bind(9999).ok());
    TEST_ASSERT_TRUE(server.listen(1).ok());
    TEST_ASSERT_TRUE(client.connect(kLoopbackAddr, 9999).ok());

    // Drain the 3-way handshake (SYN -> SYN-ACK -> ACK); server.on_accept fires.
    // The poll() budget loop advances the FSM a few rounds each call.
    for (int i = 0; i < 6; ++i) {
        stack.poll();
    }

    Ipv4Addr raddr{};
    uint16_t rport = 0;
    auto     acc   = server.accept(&raddr, &rport);
    TEST_ASSERT_TRUE(acc.ok());
    TEST_ASSERT_NOT_NULL(*acc);
    auto* child = static_cast<TcpSocket*>(*acc);

    // client -> server child.
    static const uint8_t msg[] = {'p', 'i', 'n', 'g'};
    auto                 sr    = client.send(msg, sizeof(msg));
    TEST_ASSERT_TRUE(sr.ok());
    for (int i = 0; i < 4; ++i) {
        stack.poll();
    }
    uint8_t rbuf[8] = {};
    auto    rr      = child->recv(rbuf, sizeof(rbuf), nullptr, nullptr);
    TEST_ASSERT_TRUE(rr.ok());
    TEST_ASSERT_EQ(*rr, static_cast<int64_t>(sizeof(msg)));
    bool payload_ok = true;
    for (uint32_t i = 0; i < sizeof(msg); ++i) {
        if (rbuf[i] != msg[i]) {
            payload_ok = false;
        }
    }
    TEST_ASSERT_TRUE(payload_ok);

    // echo back: server child -> client.
    auto er = child->send(rbuf, static_cast<uint32_t>(sizeof(msg)));
    TEST_ASSERT_TRUE(er.ok());
    for (int i = 0; i < 4; ++i) {
        stack.poll();
    }
    uint8_t cbuf[8] = {};
    auto    cr      = client.recv(cbuf, sizeof(cbuf), nullptr, nullptr);
    TEST_ASSERT_TRUE(cr.ok());
    TEST_ASSERT_EQ(*cr, static_cast<int64_t>(sizeof(msg)));
}

}  // namespace test_socket

extern "C" void run_socket_tests() {
    TEST_SECTION("F7-M6 socket (B1b plumbing)");
    RUN_TEST(test_socket::test_socket_dgram_returns_fd);
    RUN_TEST(test_socket::test_socket_stream_returns_fd);
    RUN_TEST(test_socket::test_socket_rejects_bad_family);
    RUN_TEST(test_socket::test_socket_rejects_bad_type);
    RUN_TEST(test_socket::test_socket_fd_routes_to_socket_stub);
    RUN_TEST(test_socket::test_udp_socket_loopback_echo);
    RUN_TEST(test_socket::test_tcp_socket_loopback_echo);
    TEST_SUMMARY();
}
