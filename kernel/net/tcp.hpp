/**
 * @file kernel/net/tcp.hpp
 * @brief TCP wire layout + TcpModule (L4Handler, IP proto 6).
 *
 * A connection-oriented L4 protocol layered on Ipv4Module exactly like
 * UdpModule: TcpModule is an L4Handler registered via
 * Ipv4Module::add_l4(kIpProtoTcp, ...).  Batch 1 delivers the wire format
 * (header parse/build + flag constants) and the inbound checksum gate: handle()
 * verifies the TCP pseudo-header checksum (proto 6) and records the segment for
 * diagnostics.  Unlike UDP a TCP checksum is MANDATORY -- 0 is a protocol
 * violation, not "no checksum" -- so the gate always verifies.  The connection
 * state machine (handshake / sequence-ACK / teardown) is layered on in
 * batches 2-3; the socket API rides on top in F7-M6.
 *
 * Namespace: cinux::net
 */

#pragma once

#include <cinux/expected.hpp>
#include <cstdint>

#include "kernel/net/ipv4.hpp"  // Ipv4Header, Ipv4Module, L4Handler, kIpProto*
#include "kernel/net/net_types.hpp"

namespace cinux::net {

/// TCP header length in 32-bit words (fixed 20-byte header, no options today).
constexpr uint8_t kTcpHdrWords = 5;

/// TCP flag bits (wire byte 13).  SYN/ACK/FIN/RST drive the FSM; PSH/URG are
/// accepted but not semantically required at this layer.
constexpr uint8_t kTcpFin = 0x01;
constexpr uint8_t kTcpSyn = 0x02;
constexpr uint8_t kTcpRst = 0x04;
constexpr uint8_t kTcpPsh = 0x08;
constexpr uint8_t kTcpAck = 0x10;
constexpr uint8_t kTcpUrg = 0x20;

/// @brief TCP header, HOST-order parsed view.  20 bytes (data offset = 5).
struct TcpHeader {
    uint16_t src_port;    ///< host order
    uint16_t dst_port;    ///< host order
    uint32_t seq;         ///< host order (sequence number)
    uint32_t ack;         ///< host order (acknowledgment number)
    uint8_t  data_off;    ///< header length in 32-bit words (raw 4-bit value)
    uint8_t  flags;       ///< FIN..CWR (low 6 bits used: kTcp*)
    uint16_t window;      ///< host order (receive window, bytes)
    uint16_t checksum;    ///< host order (pseudo-header based; MANDATORY)
    uint16_t urgent_ptr;  ///< host order
};
static_assert(sizeof(TcpHeader) == 20, "TCP fixed header is 20 bytes");

/// @brief Parse 20 wire bytes into a TcpHeader (host order).  Options bytes
///        (data_off > 5) are not parsed; callers skip them via tcp_header_bytes.
inline void parse_tcp(const uint8_t* p, TcpHeader& out) {
    out.src_port   = (static_cast<uint16_t>(p[0]) << 8) | p[1];
    out.dst_port   = (static_cast<uint16_t>(p[2]) << 8) | p[3];
    out.seq        = (static_cast<uint32_t>(p[4]) << 24) | (static_cast<uint32_t>(p[5]) << 16) |
                     (static_cast<uint32_t>(p[6]) << 8) | p[7];
    out.ack        = (static_cast<uint32_t>(p[8]) << 24) | (static_cast<uint32_t>(p[9]) << 16) |
                     (static_cast<uint32_t>(p[10]) << 8) | p[11];
    out.data_off   = static_cast<uint8_t>(p[12] >> 4);
    out.flags      = p[13];
    out.window     = (static_cast<uint16_t>(p[14]) << 8) | p[15];
    out.checksum   = (static_cast<uint16_t>(p[16]) << 8) | p[17];
    out.urgent_ptr = (static_cast<uint16_t>(p[18]) << 8) | p[19];
}

/// @brief Serialise a TcpHeader into 20 wire bytes (big-endian multi-byte; the
///        data offset sits in the hi nibble of byte 12, flags in byte 13).
inline void build_tcp_header(const TcpHeader& in, uint8_t* p) {
    p[0]  = static_cast<uint8_t>(in.src_port >> 8);
    p[1]  = static_cast<uint8_t>(in.src_port & 0xFF);
    p[2]  = static_cast<uint8_t>(in.dst_port >> 8);
    p[3]  = static_cast<uint8_t>(in.dst_port & 0xFF);
    p[4]  = static_cast<uint8_t>(in.seq >> 24);
    p[5]  = static_cast<uint8_t>(in.seq >> 16);
    p[6]  = static_cast<uint8_t>(in.seq >> 8);
    p[7]  = static_cast<uint8_t>(in.seq & 0xFF);
    p[8]  = static_cast<uint8_t>(in.ack >> 24);
    p[9]  = static_cast<uint8_t>(in.ack >> 16);
    p[10] = static_cast<uint8_t>(in.ack >> 8);
    p[11] = static_cast<uint8_t>(in.ack & 0xFF);
    p[12] = static_cast<uint8_t>(in.data_off << 4);  // reserved lo nibble zero
    p[13] = in.flags;
    p[14] = static_cast<uint8_t>(in.window >> 8);
    p[15] = static_cast<uint8_t>(in.window & 0xFF);
    p[16] = static_cast<uint8_t>(in.checksum >> 8);
    p[17] = static_cast<uint8_t>(in.checksum & 0xFF);
    p[18] = static_cast<uint8_t>(in.urgent_ptr >> 8);
    p[19] = static_cast<uint8_t>(in.urgent_ptr & 0xFF);
}

/// @brief Header length in bytes (data offset * 4).
inline uint8_t tcp_header_bytes(const TcpHeader& h) {
    return static_cast<uint8_t>(h.data_off * 4);
}

/// @brief TCP protocol layer.  Batch 1: inbound checksum gate + diagnostics.
///        Batches 2-3 add the connection table + handshake/data/teardown FSM.
class TcpModule : public L4Handler {
public:
    /// @brief L4Handler: verify the pseudo-header checksum (mandatory for TCP),
    ///        then record the segment for diagnostics.  FSM dispatch lands in
    ///        batch 2; a bad checksum is a silent drop (like UDP/ICMP).
    void handle(const Ipv4Header& ip, FrameView payload, NetDevice& dev, Ipv4Module& ipv4,
                NetStack& stack) override;

    // --- diagnostics (the checksum gate's observable, batch 1) ---
    uint32_t valid_count() const { return valid_count_; }  ///< segments past the gate
    uint16_t last_src_port() const { return last_src_port_; }
    uint16_t last_dst_port() const { return last_dst_port_; }
    uint32_t last_seq() const { return last_seq_; }
    uint32_t last_ack() const { return last_ack_; }
    uint8_t  last_flags() const { return last_flags_; }
    void     reset_diag() {
        valid_count_   = 0;
        last_src_port_ = 0;
        last_dst_port_ = 0;
        last_seq_      = 0;
        last_ack_      = 0;
        last_flags_    = 0;
    }

private:
    // Prove the checksum gate accepts valid segments and drops corrupt ones.
    // Kept as a lightweight tap alongside the FSM that arrives in batch 2.
    uint32_t valid_count_   = 0;
    uint16_t last_src_port_ = 0;
    uint16_t last_dst_port_ = 0;
    uint32_t last_seq_      = 0;
    uint32_t last_ack_      = 0;
    uint8_t  last_flags_    = 0;
};

}  // namespace cinux::net
