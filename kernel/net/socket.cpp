#include "kernel/net/socket.hpp"

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <cstdint>

#include "cinux/byte_order.hpp"
#include "cinux/checksum.hpp"
#include "cinux/memory.hpp"
#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/net/net_config.hpp"
#include "kernel/net/net_layout.hpp"
#include "kernel/net/raw.hpp"
#include "kernel/net/tcp.hpp"
#include "kernel/net/udp.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/sync.hpp"
#include "kernel/net/ifaces.hpp"
#include "kernel/syscall/sys_clock.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::net {

namespace {

using cinux::syscall::Word;

/// One boot-wide counter: every socket born gets the next number.
std::atomic<unsigned long> g_socket_identities{0};

/// The mode a socket answers stat with: a socket, owner read-write.
constexpr unsigned int kSocketMode = 0140600;

base::Result<void> socket_stat(void* /*self*/, fs::FsNode node, fs::FsStat& probed) {
    probed       = fs::FsStat{};
    probed.ino   = node.ino;
    probed.mode  = kSocketMode;
    probed.links = 1;
    return {};
}

// NOLINTNEXTLINE(readability-function-size) the FsOps signature names five
base::Result<unsigned long> socket_read(void* self, fs::FsNode /*node*/, unsigned long /*offset*/,
                                        void* sink, unsigned long count) {
    return static_cast<Socket*>(self)->receive(sink, count, nullptr);
}

// NOLINTNEXTLINE(readability-function-size) the FsOps signature names five
base::Result<unsigned long> socket_write(void* self, fs::FsNode /*node*/, unsigned long /*offset*/,
                                         const void* source, unsigned long count) {
    const SocketAddress kAnyone = {};
    return static_cast<Socket*>(self)->transmit(source, count, kAnyone);
}

void socket_retain(void* self, fs::FsNode /*node*/) {
    static_cast<Socket*>(self)->retain();
}

void socket_release(void* self, fs::FsNode /*node*/) {
    auto* const kSocket = static_cast<Socket*>(self);
    if (kSocket->release()) {
        delete kSocket;
    }
}

bool socket_readable(void* self, fs::FsNode /*node*/) {
    return static_cast<Socket*>(self)->holds_input();
}

/// The interface-question words, Linux-shaped: one ifreq in, one out.
constexpr unsigned long long kSiocgifflags   = 0x8913;
constexpr unsigned long long kSiocgifaddr    = 0x8915;
constexpr unsigned long long kSiocgifdstaddr = 0x8917;
constexpr unsigned long long kSiocgifbrdaddr = 0x8919;
constexpr unsigned long long kSiocgifnetmask = 0x891b;
constexpr unsigned long long kSiocgifmetric  = 0x891d;
constexpr unsigned long long kSiocgifmtu     = 0x8921;
constexpr unsigned long long kSiocgifhwaddr  = 0x8927;
constexpr unsigned long long kSiocgiftxqlen  = 0x8942;

/// The interface flag words the kernel answers: everything it serves
/// is up and running; the loopback adds its own kind, the wire broadcast.
constexpr unsigned short kIffUp        = 0x1;
constexpr unsigned short kIffBroadcast = 0x2;
constexpr unsigned short kIffLoopback  = 0x8;
constexpr unsigned short kIffRunning   = 0x40;

/// Bytes in one ifreq: sixteen of name, twenty-four of answer.
constexpr unsigned long kIfreqBytes   = 40;
/// The default transmit queue length the kernel books for any wire.
constexpr unsigned int  kIfTxQueueLen = 1000;

/// The one enumeration question: an ifconf is a length and a buffer,
/// and the answer is one ifreq per interface, address family included.
constexpr unsigned long long kSiocgifconf = 0x8912;
/// Bytes in one ifconf: a four-byte length, padding, an eight-byte buffer word.
constexpr unsigned long      kIfconfBytes = 16;

/// One interface enumeration over one socket: one ifreq per seat.
long long answer_ifconf(unsigned long long argument) {
    unsigned char conf[kIfconfBytes] = {};
    if (!cinux::syscall::uaccess::CopyFromUser(conf, argument, kIfconfBytes)) {
        return -cinux::syscall::kEfault;
    }
    int  have   = 0;
    Word buffer = 0;
    base::CopyBytes(&have, conf, sizeof(have));
    base::CopyBytes(&buffer, conf + 8, sizeof(buffer));
    if (buffer == 0) {
        return -cinux::syscall::kEfault;
    }

    unsigned long written = 0;
    for (unsigned long ordinal = 0;; ++ordinal) {
        char name[net::kIfNameMax] = {};
        if (!net::NameInterfaceAt(ordinal, name)) {
            break;
        }
        net::InterfaceFacts facts{};
        if (!net::ReadInterfaceFacts(name, facts)) {
            break;
        }
        if (have >= 0 && written + kIfreqBytes > static_cast<unsigned long>(have)) {
            break;
        }
        unsigned char request[kIfreqBytes] = {};
        for (unsigned long index = 0; name[index] != '\0'; ++index) {
            request[index] = static_cast<unsigned char>(name[index]);
        }
        const auto kFamily = static_cast<uint16_t>(net::kAfInet);
        base::CopyBytes(request + 16, &kFamily, sizeof(kFamily));
        base::WriteWord32Be(request, 20, facts.address);
        if (!cinux::syscall::uaccess::CopyToUser(buffer + written, request, kIfreqBytes)) {
            return -cinux::syscall::kEfault;
        }
        written += kIfreqBytes;
    }
    base::CopyBytes(conf, &written,
                    sizeof(written) < sizeof(have) ? sizeof(written) : sizeof(have));
    base::SetBytes(conf + 4, 0, 4);
    if (!cinux::syscall::uaccess::CopyToUser(argument, conf, kIfconfBytes)) {
        return -cinux::syscall::kEfault;
    }
    return 0;
}

/// One interface question over one socket: find the roll, fill the ifreq.
long long socket_if_ioctl_impl(unsigned long long word, unsigned long long argument) {
    if (word == kSiocgifconf) {
        return answer_ifconf(argument);
    }
    if (word == kSiocgifdstaddr) {
        return -cinux::syscall::kEnotty;
    }
    unsigned char request[kIfreqBytes] = {};
    if (!cinux::syscall::uaccess::CopyFromUser(request, argument, kIfreqBytes)) {
        return -cinux::syscall::kEfault;
    }
    request[kIfreqBytes - 1] = '\0';
    net::InterfaceFacts facts{};
    if (!net::ReadInterfaceFacts(reinterpret_cast<const char*>(request), facts)) {
        return -cinux::syscall::kEnodev;
    }
    const bool kLoopbackKind = facts.hardware_type == net::kHardwareLoopback;

    base::SetBytes(request + 16, 0, kIfreqBytes - 16);
    if (word == kSiocgifflags) {
        unsigned short flags = kIffUp | kIffRunning;
        flags |= kLoopbackKind ? kIffLoopback : kIffBroadcast;
        base::CopyBytes(request + 16, &flags, sizeof(flags));
    } else if (word == kSiocgifaddr || word == kSiocgifnetmask || word == kSiocgifbrdaddr) {
        uint32_t chosen = facts.broadcast;
        if (word == kSiocgifaddr) {
            chosen = facts.address;
        } else if (word == kSiocgifnetmask) {
            chosen = facts.netmask;
        }
        const auto kFamily = static_cast<uint16_t>(net::kAfInet);
        base::CopyBytes(request + 16, &kFamily, sizeof(kFamily));
        base::WriteWord32Be(request, 20, chosen);
    } else if (word == kSiocgifhwaddr) {
        const auto kKind = facts.hardware_type;
        base::CopyBytes(request + 16, &kKind, sizeof(kKind));
        base::CopyBytes(request + 18, facts.hardware.bytes, 6);
    } else if (word == kSiocgifmtu) {
        const unsigned int kMtu = facts.mtu;
        base::CopyBytes(request + 16, &kMtu, sizeof(kMtu));
    } else if (word == kSiocgifmetric) {
        const unsigned int kZero = 0;
        base::CopyBytes(request + 16, &kZero, sizeof(kZero));
    } else if (word == kSiocgiftxqlen) {
        base::CopyBytes(request + 16, &kIfTxQueueLen, sizeof(kIfTxQueueLen));
    } else {
        return -cinux::syscall::kEnotty;
    }
    if (!cinux::syscall::uaccess::CopyToUser(argument, request, kIfreqBytes)) {
        return -cinux::syscall::kEfault;
    }
    return 0;
}


/// The ops-table door the neutral ioctl walks: same answers, one seat.
long long socket_if_ioctl(void* /*self*/, fs::FsNode /*node*/, unsigned long long word,
                          unsigned long long argument) {
    return socket_if_ioctl_impl(word, argument);
}

const fs::FsOps kOps{
    .lookup     = nullptr,
    .make_file  = nullptr,
    .make_dir   = nullptr,
    .unlink     = nullptr,
    .read       = socket_read,
    .write      = socket_write,
    .read_dir   = nullptr,
    .stat       = socket_stat,
    .retain     = socket_retain,
    .release    = socket_release,
    .readable   = socket_readable,
    .ioctl      = socket_if_ioctl,
    .adopt_open = nullptr,
};

}  // namespace

Socket::Socket(int socket_type, int family)
    : type_(socket_type),
      family_(family),
      identity_(g_socket_identities.fetch_add(1, std::memory_order_relaxed) + 1) {}

unsigned short Socket::poll_events(unsigned int events) const {
    constexpr unsigned int kRead  = 1;
    constexpr unsigned int kWrite = 4;
    return static_cast<unsigned short>((events & kWrite) | (holds_input() ? events & kRead : 0));
}

UdpSocket::UdpSocket(UdpModule& module) : Socket(kSockDgram), module_(module) {}

UdpSocket::~UdpSocket() {
    const proc::PreemptGuard kPin;
    if (seated_) {
        module_.unbind(local_.port);
    }
    ring_gate_.retire();
}

base::Result<unsigned long> UdpSocket::receive(void* sink, unsigned long count,
                                               SocketAddress* from) {
    if (count == 0) {
        return 0;
    }
    const auto kGate = ring_gate_.guard();
    Waiting    taken{};
    if (!waiting_.pop(taken)) {
        return base::KernelError::kWouldBlock;
    }
    const unsigned long kBytes = taken.length < count ? taken.length : count;
    base::CopyBytes(sink, taken.bytes, kBytes);
    if (from != nullptr) {
        *from = taken.from;
    }
    return kBytes;
}

base::Result<unsigned long> UdpSocket::transmit(const void* source, unsigned long count,
                                                const SocketAddress& destination) {
    const proc::PreemptGuard kPin;
    if (count > kUdpSocketPayloadBytes) {
        return base::KernelError::kNotSupported;
    }
    SocketAddress target = destination;
    if (target.address == 0 && target.port == 0) {
        if (!named_) {
            return base::KernelError::kNotFound;
        }
        target = peer_;
    }

    if (!seated_) {
        const uint16_t kGranted = module_.ephemeral_port();
        if (kGranted == 0 || !module_.bind(kGranted, this, &UdpSocket::take)) {
            return base::KernelError::kAlreadyExists;
        }
        seated_     = true;
        local_.port = kGranted;
    }
    if (!module_.send(target.address, local_.port, target.port,
                      static_cast<const unsigned char*>(source), count)) {
        return base::KernelError::kIOError;
    }
    return count;
}

base::Result<void> UdpSocket::adopt_local(const SocketAddress& local) {
    const proc::PreemptGuard kPin;
    if (seated_) {
        return base::KernelError::kAlreadyExists;
    }
    uint16_t port = local.port;
    if (port == 0) {
        port = module_.ephemeral_port();
        if (port == 0) {
            return base::KernelError::kAlreadyExists;
        }
    }
    if (!module_.bind(port, this, &UdpSocket::take)) {
        return base::KernelError::kAlreadyExists;
    }
    local_      = local;
    local_.port = port;
    seated_     = true;
    return {};
}

base::Result<void> UdpSocket::adopt_peer(const SocketAddress& peer) {
    peer_  = peer;
    named_ = true;
    return {};
}

base::Result<void> UdpSocket::report_local(SocketAddress& local) const {
    local = local_;
    if (local.address == 0) {
        local.address = module_.own_address();
    }
    return {};
}

base::Result<void> UdpSocket::report_peer(SocketAddress& peer) const {
    if (!named_) {
        return base::KernelError::kNotFound;
    }
    peer = peer_;
    return {};
}

bool UdpSocket::holds_input() const {
    const auto kGate = ring_gate_.guard();
    return waiting_.count() != 0;
}

unsigned long UdpSocket::dropped() const {
    const auto kGate = ring_gate_.guard();
    return dropped_;
}

void UdpSocket::take(void* context, const UdpDelivery& delivery) {
    auto* const kSelf = static_cast<UdpSocket*>(context);
    const auto  kGate = kSelf->ring_gate_.guard();
    Waiting     room{};
    room.from = SocketAddress{.address = delivery.sender, .port = delivery.sender_port};
    room.from.datagram_bytes = delivery.length;
    room.length =
        delivery.length < kUdpSocketPayloadBytes ? delivery.length : kUdpSocketPayloadBytes;
    base::CopyBytes(room.bytes, delivery.bytes, room.length);
    if (!kSelf->waiting_.push(room)) {
        ++kSelf->dropped_;
    }
}

RawSocket::RawSocket(RawModule& module) : Socket(kSockRaw), module_(module) {
    seated_ = module_.bind(this, take);
}

RawSocket::~RawSocket() {
    if (seated_) {
        module_.unbind(this);
    }
    gate_.retire();
}

base::Result<unsigned long> RawSocket::receive(void* sink, unsigned long count,
                                               SocketAddress* from) {
    const auto kGuard = gate_.guard();
    Waiting    taken{};
    if (!waiting_.pop(taken)) {
        return base::KernelError::kWouldBlock;
    }
    const auto kBytes = taken.length < count ? taken.length : count;
    base::CopyBytes(sink, taken.bytes, kBytes);
    if (from != nullptr) {
        *from = taken.from;
    }
    return kBytes;
}

base::Result<unsigned long> RawSocket::transmit(const void* source, unsigned long count,
                                                const SocketAddress& destination) {
    SocketAddress target = destination;
    {
        const auto kGuard = gate_.guard();
        if (target.address == 0 && named_) {
            target = peer_;
        }
    }
    if (target.address == 0) {
        return base::KernelError::kNotFound;
    }
    if (count > kMaxDatagramBytes - kMinIpv4HeaderBytes) {
        return base::KernelError::kNotSupported;
    }
    if (!module_.send(target.address, static_cast<const unsigned char*>(source), count)) {
        return base::KernelError::kIOError;
    }
    return count;
}

base::Result<void> RawSocket::adopt_local(const SocketAddress& local) {
    const auto kGuard = gate_.guard();
    local_            = local;
    return {};
}

base::Result<void> RawSocket::adopt_peer(const SocketAddress& peer) {
    const auto kGuard = gate_.guard();
    peer_             = peer;
    named_            = true;
    return {};
}

base::Result<void> RawSocket::report_local(SocketAddress& local) const {
    const auto kGuard = gate_.guard();
    local             = local_;
    return {};
}

base::Result<void> RawSocket::report_peer(SocketAddress& peer) const {
    const auto kGuard = gate_.guard();
    if (!named_) {
        return base::KernelError::kNotFound;
    }
    peer = peer_;
    return {};
}

bool RawSocket::holds_input() const {
    const auto kGuard = gate_.guard();
    return waiting_.count() != 0;
}

void RawSocket::take(void* context, const unsigned char* bytes, size_t length,
                     const Ipv4View& header) {
    auto&      self   = *static_cast<RawSocket*>(context);
    const auto kGuard = self.gate_.guard();
    if ((self.local_.address != 0 && self.local_.address != header.destination) ||
        (self.named_ && self.peer_.address != header.source)) {
        return;
    }
    Waiting entry{};
    entry.from.address = header.source;
    entry.from.received_ttl = bytes[8];
    entry.from.received_ns = syscall::RealtimeNanoseconds();
    entry.from.datagram_bytes = length;
    entry.length       = length;
    base::CopyBytes(entry.bytes, bytes, length);
    static_cast<void>(self.waiting_.push(entry));
}

PingSocket::PingSocket(RawModule& module)
    : Socket(kSockDgram), module_(module), identifier_(static_cast<std::uint16_t>(identity())) {
    seated_ = module_.bind(this, take);
}

PingSocket::~PingSocket() {
    if (seated_) {
        module_.unbind(this);
    }
    gate_.retire();
}

base::Result<unsigned long> PingSocket::receive(void* sink, unsigned long count,
                                                SocketAddress* from) {
    if (count == 0) {
        return 0;
    }
    const auto kGuard = gate_.guard();
    Waiting    taken{};
    if (!waiting_.pop(taken)) {
        return base::KernelError::kWouldBlock;
    }
    const unsigned long kBytes = taken.length < count ? taken.length : count;
    base::CopyBytes(sink, taken.bytes, kBytes);
    if (from != nullptr) {
        *from = taken.from;
    }
    return kBytes;
}

base::Result<unsigned long> PingSocket::transmit(const void* source, unsigned long count,
                                                 const SocketAddress& destination) {
    SocketAddress target     = destination;
    std::uint16_t identifier = 0;
    {
        const auto kGuard = gate_.guard();
        if (target.address == 0 && named_) {
            target = peer_;
        }
        identifier = identifier_;
    }
    if (target.address == 0) {
        return base::KernelError::kNotFound;
    }
    const auto* message = static_cast<const unsigned char*>(source);
    if (count < kIcmpEchoHeaderBytes || message[0] != kIcmpEchoRequest || message[1] != 0) {
        return base::KernelError::kInvalidArgument;
    }
    if (count > kMaxIcmpEchoBytes) {
        return base::KernelError::kNotSupported;
    }
    unsigned char packet[kMaxIcmpEchoBytes] = {};
    base::CopyBytes(packet, message, count);
    base::WriteWord16Be(packet, 2, 0);
    base::WriteWord16Be(packet, 4, identifier);
    base::InternetChecksum checksum;
    checksum.add(packet, count);
    base::WriteWord16Be(packet, 2, checksum.value());
    if (!module_.send(target.address, packet, count)) {
        return base::KernelError::kIOError;
    }
    return count;
}

base::Result<void> PingSocket::adopt_local(const SocketAddress& local) {
    const auto kGuard = gate_.guard();
    local_            = local;
    if (local.port != 0) {
        identifier_ = local.port;
    }
    return {};
}

base::Result<void> PingSocket::adopt_peer(const SocketAddress& peer) {
    const auto kGuard = gate_.guard();
    peer_             = peer;
    named_            = true;
    return {};
}

base::Result<void> PingSocket::report_local(SocketAddress& local) const {
    const auto kGuard = gate_.guard();
    local             = local_;
    local.port        = identifier_;
    return {};
}

base::Result<void> PingSocket::report_peer(SocketAddress& peer) const {
    const auto kGuard = gate_.guard();
    if (!named_) {
        return base::KernelError::kNotFound;
    }
    peer = peer_;
    return {};
}

bool PingSocket::holds_input() const {
    const auto kGuard = gate_.guard();
    return waiting_.count() != 0;
}

void PingSocket::take(void* context, const unsigned char* bytes, size_t length,
                      const Ipv4View& header) {
    auto& self = *static_cast<PingSocket*>(context);
    if (length < header.header_bytes + kIcmpEchoHeaderBytes) {
        return;
    }
    const unsigned char* icmp   = bytes + header.header_bytes;
    const auto kIcmpLength = length - header.header_bytes;
    base::InternetChecksum checksum;
    checksum.add(icmp, kIcmpLength);
    if (kIcmpLength > kMaxIcmpEchoBytes || icmp[1] != 0 || checksum.value() != 0) {
        return;
    }
    const auto           kGuard = self.gate_.guard();
    if (icmp[0] != kIcmpEchoReply || base::ReadWord16Be(icmp, 4) != self.identifier_) {
        return;
    }
    if ((self.local_.address != 0 && self.local_.address != header.destination) ||
        (self.named_ && self.peer_.address != header.source)) {
        return;
    }
    Waiting entry{};
    entry.from.address = header.source;
    entry.length       = length - header.header_bytes;
    entry.from.received_ttl = bytes[8];
    entry.from.received_ns = syscall::RealtimeNanoseconds();
    entry.from.datagram_bytes = entry.length;
    base::CopyBytes(entry.bytes, icmp, entry.length);
    static_cast<void>(self.waiting_.push(entry));
}

base::Result<void> Socket::begin_listen(unsigned long /*backlog*/) {
    return base::KernelError::kNotSupported;
}

base::Result<base::Shared<Socket>> Socket::accept_one() {
    return base::KernelError::kNotSupported;
}

base::Result<void> Socket::shutdown_write() {
    return base::KernelError::kNotConnected;
}

bool Socket::handshake_pending() const {
    return false;
}

bool Socket::handshake_failed() const {
    return false;
}

TcpSocket::TcpSocket(TcpModule& module) : Socket(kSockStream), module_(module) {}

TcpSocket::~TcpSocket() {
    const proc::PreemptGuard kPin;
    if (listening_) {
        module_.unlisten(listener_);
    } else if (opened_) {
        module_.close(connection_);
    }
}

base::Result<unsigned long> TcpSocket::receive(void* sink, unsigned long count,
                                               SocketAddress* from) {
    if (count == 0) {
        return 0;
    }
    if (!opened_) {
        return base::KernelError::kNotConnected;
    }
    const auto kTaken = module_.receive(connection_, static_cast<unsigned char*>(sink), count);
    if (!kTaken.ok()) {
        if (kTaken.error() == base::KernelError::kNotFound) {
            return static_cast<unsigned long>(0);
        }
        return kTaken.error();
    }
    if (from != nullptr) {
        *from = peer_;
    }
    return kTaken.value();
}

base::Result<unsigned long> TcpSocket::transmit(const void* source, unsigned long count,
                                                const SocketAddress& /*destination*/) {
    const proc::PreemptGuard kPin;
    if (!opened_) {
        return base::KernelError::kNotConnected;
    }
    const auto kGiven = module_.send(connection_, static_cast<const unsigned char*>(source), count,
                                     module_.clock());
    if (!kGiven.ok()) {
        return kGiven.error();
    }
    return kGiven.value();
}

base::Result<void> TcpSocket::adopt_local(const SocketAddress& local) {
    const proc::PreemptGuard kPin;
    if (local.address != 0 && local.address != module_.own_address() &&
        local.address != kLoopbackAddress) {
        return base::KernelError::kInvalidArgument;
    }
    if (bound_ || opened_) {
        return base::KernelError::kAlreadyExists;
    }
    local_ = local;
    bound_ = true;
    return {};
}

base::Result<void> TcpSocket::adopt_peer(const SocketAddress& peer) {
    const proc::PreemptGuard kPin;
    if (listening_) {
        return base::KernelError::kNotSupported;
    }
    if (peer.address == 0 || peer.port == 0) {
        return base::KernelError::kInvalidArgument;
    }
    if (opened_) {
        return base::KernelError::kAlreadyExists;
    }
    const TcpEvents kNoNews{};
    const auto      kOpened =
        module_.connect(local_.port, peer.address, peer.port, kNoNews, module_.clock());
    if (!kOpened.ok()) {
        return kOpened.error();
    }
    peer_       = peer;
    connection_ = kOpened.value();
    opened_     = true;
    return {};
}

base::Result<void> TcpSocket::report_local(SocketAddress& local) const {
    local = local_;
    return {};
}

base::Result<void> TcpSocket::report_peer(SocketAddress& peer) const {
    if (!opened_) {
        return base::KernelError::kNotConnected;
    }
    peer = peer_;
    return {};
}

bool TcpSocket::holds_input() const {
    if (!opened_) {
        return false;
    }
    return module_.holds_bytes(connection_);
}

base::Result<void> TcpSocket::begin_listen(unsigned long /*backlog*/) {
    const proc::PreemptGuard kPin;
    if (!bound_ || opened_) {
        return base::KernelError::kInvalidArgument;
    }
    if (listening_) {
        return {};
    }
    const auto kSeat = module_.listen(local_.port, {});
    if (!kSeat.ok()) {
        return kSeat.error();
    }
    listener_  = kSeat.value();
    seated_    = true;
    listening_ = true;
    return {};
}

base::Result<base::Shared<Socket>> TcpSocket::accept_one() {
    if (!seated_) {
        return base::KernelError::kNotSupported;
    }
    const auto kTaken = module_.take_accepted(listener_);
    if (!kTaken.ok()) {
        return kTaken.error();
    }
    auto*                born = new TcpSocket(module_);
    base::Shared<Socket> face{born};
    born->connection_ = kTaken.value();
    born->opened_     = true;
    born->local_      = local_;
    uint32_t address  = 0;
    uint16_t port     = 0;
    if (module_.peer_of(kTaken.value(), address, port)) {
        born->peer_ = SocketAddress{.address = address, .port = port};
    }
    return face;
}

bool TcpSocket::handshake_pending() const {
    return opened_ && module_.connecting(connection_);
}

base::Result<void> TcpSocket::shutdown_write() {
    const proc::PreemptGuard kPin;
    if (!opened_ || !module_.alive(connection_) || handshake_pending()) {
        return base::KernelError::kNotConnected;
    }
    module_.close(connection_);
    return {};
}

bool TcpSocket::handshake_failed() const {
    return opened_ && !module_.established(connection_) && !module_.alive(connection_);
}

const fs::FsOps* SocketOps() {
    return &kOps;
}

}  // namespace cinux::net
