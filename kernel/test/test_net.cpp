/**
 * @file kernel/test/test_net.cpp
 * @brief F7 L1 in-kernel test: full L3 stack on loopback, ping 127.0.0.1.
 *
 * Deterministic -- no QEMU user-net / SLIRP timing.  A LoopbackDevice (software,
 * no L2) carries an ICMP echo request round-trip through the real NetStack
 * dispatch -> Ipv4Module -> IcmpModule -> echo reply, all in one poll().  Proves
 * the L3 stack end-to-end before any hardware NIC is attached (L2 does that).
 *
 * LoopbackDevice is ~12 KB -> allocated STATICALLY (the 16 KB kernel stack
 * cannot hold one).
 */

#include <cstdint>

#include "big_kernel_test.h"
#include "kernel/lib/kprintf.hpp"
#include "kernel/net/arp.hpp"
#include "kernel/net/icmp.hpp"
#include "kernel/net/ipv4.hpp"
#include "kernel/net/loopback_device.hpp"
#include "kernel/net/net_stack.hpp"

using cinux::net::ArpModule;
using cinux::net::IcmpModule;
using cinux::net::InDevice;
using cinux::net::Ipv4Module;
using cinux::net::kEtherTypeArp;
using cinux::net::kEtherTypeIpv4;
using cinux::net::kLoopbackAddr;
using cinux::net::LoopbackDevice;
using cinux::net::NetStack;

namespace test_net {

void test_ping_loopback() {
    // Static: each LoopbackDevice is ~12 KB; the 16 KB kernel stack cannot hold one.
    static LoopbackDevice lo;
    static ArpModule      arp;
    static IcmpModule     icmp;
    static Ipv4Module     ipv4(icmp, &arp);
    static NetStack       stack;

    stack.add_protocol(kEtherTypeArp, arp);
    stack.add_protocol(kEtherTypeIpv4, ipv4);

    InDevice cfg{};
    cfg.local   = kLoopbackAddr;  // 127.0.0.1
    cfg.gateway = kLoopbackAddr;
    TEST_ASSERT_TRUE(stack.attach(lo, cfg));

    icmp.reset();
    // Send an ICMP echo request to 127.0.0.1 (loopback: no ARP, no L2).
    auto r = icmp.send_echo_request(lo, kLoopbackAddr, 0xABCD, 1, ipv4, stack);
    TEST_ASSERT_TRUE(r.ok());

    // One poll() drains the full round-trip (budget loop):
    //   request -> IPv4 -> ICMP echo-request handler -> reply -> IPv4 -> record.
    stack.poll();

    TEST_ASSERT_EQ(icmp.reply_count(), 1u);
    TEST_ASSERT_EQ(icmp.last_reply_id(), 0xABCDu);
    TEST_ASSERT_EQ(icmp.last_reply_seq(), 1u);
    cinux::lib::kprintf("[net] loopback ping: reply id=0x%04x seq=%u (round-trip in one poll)\n",
                        icmp.last_reply_id(), static_cast<unsigned>(icmp.last_reply_seq()));
}

}  // namespace test_net

extern "C" void run_net_tests() {
    TEST_SECTION("net");
    RUN_TEST(test_net::test_ping_loopback);
    TEST_SUMMARY();
}
