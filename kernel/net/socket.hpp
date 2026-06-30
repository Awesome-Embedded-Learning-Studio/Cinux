/**
 * @file kernel/net/socket.hpp
 * @brief Socket base + SocketOps (InodeOps shim) + sockaddr UAPI (F7-M6).
 *
 * The user-visible socket API rides on the EXISTING fd layer exactly like
 * PTY/pipe: a socket fd is File -> Inode -> SocketOps (an InodeOps subclass),
 * so sys_read/write/ioctl/close dispatch through ops with ZERO changes to the fd
 * layer.  ONE stateless SocketOps instance serves every socket inode; the
 * per-fd state (the Socket*) lives in inode->fs_private (the PTY pattern).
 *
 * Socket is a concrete base with stub virtuals (return NotImplemented): the fd
 * machinery is exercisable BEFORE any protocol adapter exists (batch 1b), and a
 * bare Socket answers socket()/close() while rejecting protocol ops.  B2 (UDP)
 * and B3 (TCP) subclass it -- UdpSocket/UdpListener and TcpSocket/TcpListener --
 * overriding bind/connect/listen/accept/send/recv and pushing inbound frames
 * (copied out of the borrowed on_udp/on_data payload) into a per-socket RX ring.
 *
 * The protocol modules (TcpModule/UdpModule, F7-M4/M5) stay PURE: blocking +
 * buffering live here, in the adapter, not in the FSM (CODING-TASTE §13).
 *
 * Namespace: cinux::net
 */

#pragma once

#include <cinux/expected.hpp>
#include <cstdint>

#include "kernel/fs/inode.hpp"      // InodeOps
#include "kernel/net/net_types.hpp"  // Ipv4Addr

namespace cinux::net {

// ============================================================
// UAPI constants (mirror Linux userspace values; musl compiles against these)
// ============================================================

constexpr int kAfInet     = 2;  ///< AF_INET (IPv4)
constexpr int kSockStream = 1;  ///< TCP
constexpr int kSockDgram  = 2;  ///< UDP

/// IPv4 socket address -- kernel mirror of libc sockaddr_in (16 bytes).
/// @note port is NETWORK order on the wire (musl lays it out big-endian); the
///       syscall handler ntohs()s it into host order before handing it to a
///       Socket.  family is host order (musl writes 2).
struct SockAddrIn {
    uint16_t family;     ///< AF_INET (host order)
    uint16_t port;       ///< NETWORK order (big-endian)
    uint8_t  addr[4];    ///< IPv4 address (network byte order)
    uint8_t  zero[8];    ///< padding to sizeof(sockaddr) = 16
};
static_assert(sizeof(SockAddrIn) == 16, "sockaddr_in is 16 bytes");

class Socket;

/// @brief Socket base -- the per-fd connection state + protocol-agnostic API.
///
/// Default virtuals return Error::NotImplemented so batch 1b can land the fd +
/// syscall plumbing with no protocol adapter: socket() succeeds, close() works,
/// and bind/connect/send/recv answer "not implemented" (-ENOSYS) until B2/B3
/// override them.  Subclasses ALSO implement UdpListener / TcpListener so the
/// protocol modules push inbound bytes INTO the socket (the modules stay pure).
class Socket {
public:
    Socket(int domain, int type) : domain_(domain), type_(type) {}
    virtual ~Socket() = default;

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    /// @name Protocol API (overridden by UdpSocket / TcpSocket in B2 / B3).
    ///@{
    virtual cinux::lib::ErrorOr<void>     bind(uint16_t local_port);
    virtual cinux::lib::ErrorOr<void>     connect(Ipv4Addr remote, uint16_t remote_port);
    virtual cinux::lib::ErrorOr<void>     listen(int backlog);
    /// Passive accept: return a NEW Socket* for a completed connection (TCP).
    /// The caller (sys_accept) installs it under a fresh fd.
    virtual cinux::lib::ErrorOr<Socket*>  accept(Ipv4Addr* out_remote, uint16_t* out_port);
    /// Send on a connected socket (TCP established, or UDP after connect()).
    virtual cinux::lib::ErrorOr<int64_t>  send(const uint8_t* buf, uint32_t len);
    /// Send a datagram to an explicit destination (UDP sendto).
    virtual cinux::lib::ErrorOr<int64_t>  sendto(Ipv4Addr dst, uint16_t dst_port, const uint8_t* buf,
                                                 uint32_t len);
    /// Receive; on success fills @p out_src/@p out_port (pass nullptr to ignore).
    /// Blocks on a per-socket wait queue when no data is available (B2/B3).
    virtual cinux::lib::ErrorOr<int64_t>  recv(uint8_t* buf, uint32_t len, Ipv4Addr* out_src,
                                               uint16_t* out_port);
    /// Release protocol resources (unbind / stop_listen / FIN).  No-op by default.
    virtual void                          close();
    ///@}

    int domain() const { return domain_; }  ///< AF_INET
    int type() const { return type_; }      ///< SOCK_STREAM / SOCK_DGRAM

protected:
    int domain_;  ///< AF_INET
    int type_;    ///< SOCK_STREAM / SOCK_DGRAM
};

/// @brief InodeOps shim -- makes a socket fd indistinguishable from a pipe fd to
///        the fd layer.  ONE shared stateless instance (socket_ops()); the
///        per-fd Socket lives in inode->fs_private.
///
/// read() -> Socket::recv, write() -> Socket::send.  The addr-aware operations
/// (bind/connect/listen/accept/sendto/recvfrom) go through the socket syscalls,
/// NOT read/write -- sys_read/sys_write are the byte-stream face a connected
/// socket shows to generic code (and to musl read()/write() on the fd).
class SocketOps : public cinux::fs::InodeOps {
public:
    cinux::lib::ErrorOr<int64_t> read(const cinux::fs::Inode* inode, uint64_t /*offset*/, void* buf,
                                      uint64_t count) override;
    cinux::lib::ErrorOr<int64_t> write(cinux::fs::Inode* inode, uint64_t /*offset*/, const void* buf,
                                       uint64_t count) override;
    // ioctl/stat/open inherit the InodeOps defaults (NotImplemented/-1/false) --
    // a socket is not a tty, not a disk file, not a cloning device.
};

/// @brief The single shared SocketOps instance every socket inode points at.
SocketOps& socket_ops();

}  // namespace cinux::net
