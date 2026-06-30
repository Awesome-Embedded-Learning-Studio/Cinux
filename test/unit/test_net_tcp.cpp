/**
 * @file test/unit/test_net_tcp.cpp
 * @brief Host unit tests for the TCP wire layer + the inbound checksum gate
 *        (batch 1).  TcpModule is registered as L4Handler proto 6; a hand-built
 *        TCP segment over IPv4 is fed through the real Ipv4Module dispatch and
 *        the checksum gate's diagnostics are inspected.  The connection FSM is
 *        exercised in batches 2-3.
 *
 * Links the real tcp.cpp + ipv4.cpp + icmp.cpp + arp.cpp + net_stack.cpp +
 * Cinux-Base checksum.cpp (pure logic, host-linkable).
 *
 * Compile condition: -DCINUX_HOST_TEST
 */

#define TEST_FRAMEWORK_IMPL

#include <cinux/checksum.hpp>
#include <cinux/expected.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

#include "kernel/net/icmp.hpp"
#include "kernel/net/ipv4.hpp"
#include "kernel/net/net_device.hpp"
#include "kernel/net/net_stack.hpp"
#include "kernel/net/tcp.hpp"
#include "test_framework.h"

using cinux::lib::internet_checksum;
using cinux::net::InDevice;
using cinux::net::Ipv4Addr;
using cinux::net::Ipv4Module;
using cinux::net::IcmpModule;
using cinux::net::kEtherTypeIpv4;
using cinux::net::kIpProtoTcp;
using cinux::net::NetDevice;
using cinux::net::NetStack;
using cinux::net::Packet;
using cinux::net::TcpHeader;
using cinux::net::TcpModule;

namespace {

Ipv4Addr ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return Ipv4Addr{{a, b, c, d}};
}

/// No-L2 mock (same shape as test_net_udp): yields queued RX frames.  TX is
/// unused in batch 1 (no send path yet); batches 2-3 will capture send_l3.
class NoL2Dev : public NetDevice {
public:
    std::vector<std::vector<uint8_t>> rx;
    size_t                            idx = 0;

    bool mac(cinux::net::EthAddr&) const override { return false; }
    bool has_ethernet_header() const override { return false; }
    bool poll_rx(Packet& out) override {
        if (idx >= rx.size()) {
            return false;
        }
        out.data = rx[idx].data();
        out.len  = static_cast<uint32_t>(rx[idx].size());
        out.sink = nullptr;
        ++idx;
        return true;
    }
    cinux::lib::ErrorOr<void> send_l3(const cinux::net::EthAddr&, uint16_t, const uint8_t*,
                                      uint32_t) override {
        return {};
    }
};

/// Stack + modules wired for a no-L2 device at @p local.  TCP joins ICMP (auto)
/// in the L4 table via add_l4(proto 6).
struct Fixture {
    NoL2Dev    dev;
    IcmpModule icmp;
    Ipv4Module ipv4;
    TcpModule  tcp;
    NetStack   stack;
    explicit Fixture(Ipv4Addr local) : ipv4(icmp, nullptr) {
        stack.add_protocol(kEtherTypeIpv4, ipv4);
        ipv4.add_l4(kIpProtoTcp, tcp);
        InDevice cfg{};
        cfg.local   = local;
        cfg.gateway = local;
        stack.attach(dev, cfg);
    }
};

/// Build a 20-byte IPv4 header (valid checksum) wrapping @p l4 + emit the packet.
std::vector<uint8_t> ip_packet(Ipv4Addr src, Ipv4Addr dst, uint8_t proto,
                               const std::vector<uint8_t>& l4) {
    std::vector<uint8_t> p(20 + l4.size(), 0);
    p[0]                 = 0x45;  // version 4, IHL 5
    const uint16_t total = static_cast<uint16_t>(20 + l4.size());
    p[2]                 = static_cast<uint8_t>(total >> 8);
    p[3]                 = static_cast<uint8_t>(total & 0xFF);
    p[8]                 = 64;  // TTL
    p[9]                 = proto;
    for (int i = 0; i < 4; ++i) {
        p[12 + i] = src.oct[i];
        p[16 + i] = dst.oct[i];
    }
    std::memcpy(p.data() + 20, l4.data(), l4.size());
    const uint16_t cs = internet_checksum(p.data(), 20);
    p[10]             = static_cast<uint8_t>(cs >> 8);
    p[11]             = static_cast<uint8_t>(cs & 0xFF);
    return p;
}

/// Embed the TCP pseudo-header checksum into @p seg's checksum field (bytes
/// 16-17).  Mirrors the TX path that TcpModule::send_segment will use (batch 2).
void embed_tcp_checksum(std::vector<uint8_t>& seg, Ipv4Addr src, Ipv4Addr dst) {
    std::vector<uint8_t> buf(12 + seg.size(), 0);
    for (int i = 0; i < 4; ++i) {
        buf[i]     = src.oct[i];
        buf[4 + i] = dst.oct[i];
    }
    buf[9]  = kIpProtoTcp;
    buf[10] = static_cast<uint8_t>(seg.size() >> 8);
    buf[11] = static_cast<uint8_t>(seg.size() & 0xFF);
    std::memcpy(buf.data() + 12, seg.data(), seg.size());  // checksum field still 0
    uint16_t cs = internet_checksum(buf.data(), buf.size());
    if (cs == 0) {
        cs = 0xFFFF;  // TCP mandates a nonzero checksum
    }
    seg[16] = static_cast<uint8_t>(cs >> 8);
    seg[17] = static_cast<uint8_t>(cs & 0xFF);
}

/// Build a checksummed TCP segment (20-byte header + optional payload).
std::vector<uint8_t> tcp_segment(Ipv4Addr src, Ipv4Addr dst, uint16_t sp, uint16_t dp, uint32_t seq,
                                 uint32_t ack, uint8_t flags,
                                 const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> seg(20 + data.size(), 0);
    seg[0]  = static_cast<uint8_t>(sp >> 8);
    seg[1]  = static_cast<uint8_t>(sp & 0xFF);
    seg[2]  = static_cast<uint8_t>(dp >> 8);
    seg[3]  = static_cast<uint8_t>(dp & 0xFF);
    seg[4]  = static_cast<uint8_t>(seq >> 24);
    seg[5]  = static_cast<uint8_t>(seq >> 16);
    seg[6]  = static_cast<uint8_t>(seq >> 8);
    seg[7]  = static_cast<uint8_t>(seq & 0xFF);
    seg[8]  = static_cast<uint8_t>(ack >> 24);
    seg[9]  = static_cast<uint8_t>(ack >> 16);
    seg[10] = static_cast<uint8_t>(ack >> 8);
    seg[11] = static_cast<uint8_t>(ack & 0xFF);
    seg[12] = static_cast<uint8_t>(5 << 4);  // data offset = 5 (20 bytes)
    seg[13] = flags;
    seg[14] = 0x20;  // window = 8192 (arbitrary nonzero)
    seg[15] = 0x00;
    // checksum bytes 16-17 left 0 for embed_tcp_checksum
    if (!data.empty()) {
        std::memcpy(seg.data() + 20, data.data(), data.size());
    }
    embed_tcp_checksum(seg, src, dst);
    return seg;
}

/// Feed @p pkt back through the stack as a received frame, then drain one poll.
void deliver(Fixture& f, const std::vector<uint8_t>& pkt) {
    f.dev.rx.push_back(pkt);
    f.stack.poll();
}

}  // namespace

// ============================================================
// Header parse/build
// ============================================================

TEST("tcp_header: build then parse round-trips every field (big-endian wire)") {
    TcpHeader in{};
    in.src_port   = 0x1234;
    in.dst_port   = 0xBEEF;
    in.seq        = 0x11223344;
    in.ack        = 0xDEADBEEF;
    in.data_off   = 5;
    in.flags      = 0x18;  // PSH|ACK
    in.window     = 0xFFFF;
    in.checksum   = 0xF00D;
    in.urgent_ptr = 0x0001;
    uint8_t wire[20];
    build_tcp_header(in, wire);
    // data offset sits in the hi nibble of byte 12; flags in byte 13; seq/ack
    // are 32-bit big-endian.
    ASSERT_EQ(wire[12], 0x50u);
    ASSERT_EQ(wire[13], 0x18u);
    ASSERT_EQ(wire[4], 0x11u);   // seq top byte
    ASSERT_EQ(wire[11], 0xEFu);  // ack low byte
    TcpHeader out;
    parse_tcp(wire, out);
    ASSERT_EQ(out.src_port, 0x1234u);
    ASSERT_EQ(out.dst_port, 0xBEEFu);
    ASSERT_EQ(out.seq, 0x11223344u);
    ASSERT_EQ(out.ack, 0xDEADBEEFu);
    ASSERT_EQ(out.data_off, 5u);
    ASSERT_EQ(out.flags, 0x18u);
    ASSERT_EQ(out.window, 0xFFFFu);
    ASSERT_EQ(out.checksum, 0xF00Du);
    ASSERT_EQ(out.urgent_ptr, 0x0001u);
    ASSERT_EQ(tcp_header_bytes(out), 20u);
}

// ============================================================
// Inbound checksum gate (the batch-1 handle() observable)
// ============================================================

TEST("tcp_handle: a valid segment passes the checksum gate + is recorded") {
    Fixture f{ip(127, 0, 0, 1)};
    auto    seg =
        tcp_segment(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 1234, 7777, 1000, 2000, 0x10 /*ACK*/);
    deliver(f, ip_packet(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 6, seg));

    ASSERT_EQ(f.tcp.valid_count(), 1u);
    ASSERT_EQ(f.tcp.last_src_port(), 1234u);
    ASSERT_EQ(f.tcp.last_dst_port(), 7777u);
    ASSERT_EQ(f.tcp.last_seq(), 1000u);
    ASSERT_EQ(f.tcp.last_ack(), 2000u);
    ASSERT_EQ(f.tcp.last_flags(), 0x10u);
}

TEST("tcp_handle: a corrupt segment fails the checksum gate -> dropped") {
    Fixture f{ip(127, 0, 0, 1)};
    auto    seg =
        tcp_segment(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 1234, 7777, 1000, 2000, 0x10, {'h', 'i'});
    seg[20] ^= 0xFF;  // corrupt the first data byte (covered by the checksum)
    deliver(f, ip_packet(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 6, seg));

    ASSERT_EQ(f.tcp.valid_count(), 0u);  // bad checksum -> silent drop
}

TEST("tcp_handle: proto 6 reaches TcpModule via the L4 dispatch table") {
    Fixture f{ip(127, 0, 0, 1)};
    auto    syn =
        tcp_segment(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 4321, 80, 0x40000000, 0, 0x02 /*SYN*/);
    deliver(f, ip_packet(ip(127, 0, 0, 1), ip(127, 0, 0, 1), 6, syn));

    ASSERT_EQ(f.tcp.valid_count(), 1u);
    ASSERT_EQ(f.tcp.last_flags(), 0x02u);  // SYN dispatched to TCP, not eaten elsewhere
}

int main() {
    RUN_ALL_TESTS();
    return _tests_failed > 0 ? 1 : 0;
}
