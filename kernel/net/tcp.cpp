/**
 * @file kernel/net/tcp.cpp
 * @brief TcpModule -- inbound pseudo-header checksum gate (batch 1).
 *
 * handle() verifies the TCP checksum over the same 12-byte pseudo-header UDP
 * uses (src IP, dst IP, zero, proto=6, TCP length) + the TCP segment, then
 * records the segment for diagnostics.  Unlike UDP a TCP checksum is
 * MANDATORY -- 0 is a protocol violation, not "no checksum" -- so the gate
 * always verifies.  The connection state machine (handshake / sequence-ACK /
 * teardown) is layered on in batches 2-3.  Zero kprintf.
 *
 * Namespace: cinux::net
 */

#include "kernel/net/tcp.hpp"

#include <cinux/checksum.hpp>
#include <cstring>

#include "kernel/net/net_stack.hpp"  // InDevice, NetStack

namespace cinux::net {

namespace {

/// Local RAII heap buffer (no <memory>): the RX checksum buffer (up to ~1.5KB)
/// stays off the kernel stack.  See ipv4.cpp's HeapBuf for the freestanding
/// rationale (new[]/delete[] via crt_stub, not <memory>).
struct HeapBuf {
    uint8_t* p;
    explicit HeapBuf(size_t n) : p(new uint8_t[n]) {}
    ~HeapBuf() { delete[] p; }
    HeapBuf(const HeapBuf&)            = delete;
    HeapBuf& operator=(const HeapBuf&) = delete;
};

/// @brief Write the 12-byte TCP pseudo-header at @p ph (proto 6).  @p tcp_len is
///        the TCP segment length (header + data); the receiver sums the SAME run
///        the sender did, so verify matches compute.  Identical to UDP's, bar
///        the protocol byte.
void build_pseudo_header(uint8_t* ph, Ipv4Addr src, Ipv4Addr dst, uint16_t tcp_len) {
    for (int i = 0; i < 4; ++i) {
        ph[i]     = src.oct[i];
        ph[4 + i] = dst.oct[i];
    }
    ph[8]  = 0;  // zero
    ph[9]  = kIpProtoTcp;
    ph[10] = static_cast<uint8_t>(tcp_len >> 8);
    ph[11] = static_cast<uint8_t>(tcp_len & 0xFF);
}

}  // namespace

void TcpModule::handle(const Ipv4Header& ip, FrameView payload, NetDevice& /*dev*/,
                       Ipv4Module& /*ipv4*/, NetStack& /*stack*/) {
    if (payload.size() < sizeof(TcpHeader)) {
        return;  // short / not a TCP segment
    }
    // TCP checksum is MANDATORY (no "no checksum" option, unlike UDP): reconstruct
    // the run the sender summed -- pseudo-header(12, TCP length = the delivered
    // segment bytes) + the whole segment (with embedded checksum) -- and verify.
    // The L4 payload handed here IS the whole TCP segment (IPv4 stripped its
    // header), so payload.size() is the TCP length.  A bad checksum is a silent
    // drop, matching UDP/ICMP.
    const uint16_t tcp_len = static_cast<uint16_t>(payload.size());
    HeapBuf        buf(12 + tcp_len);
    build_pseudo_header(buf.p, ip.src, ip.dst, tcp_len);
    std::memcpy(buf.p + 12, payload.data(), tcp_len);
    if (!cinux::lib::verify_internet_checksum(buf.p, 12 + tcp_len)) {
        return;  // corrupt -> silently drop
    }

    TcpHeader h;
    parse_tcp(payload.data(), h);
    ++valid_count_;
    last_src_port_ = h.src_port;
    last_dst_port_ = h.dst_port;
    last_seq_      = h.seq;
    last_ack_      = h.ack;
    last_flags_    = h.flags;
}

}  // namespace cinux::net
