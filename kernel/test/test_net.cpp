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
#include "kernel/arch/x86_64/irq.hpp"  // F7 L2: sti/hlt for e1000 SLIRP timing
#include "kernel/drivers/net/e1000.hpp"
#include "kernel/drivers/net/e1000_net_device.hpp"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/net/arp.hpp"
#include "kernel/net/icmp.hpp"
#include "kernel/net/ipv4.hpp"
#include "kernel/net/loopback_device.hpp"
#include "kernel/net/net_stack.hpp"

using cinux::drivers::net::E1000Controller;
using cinux::drivers::net::E1000NetDevice;
using cinux::drivers::pci::PCIDevice;
using cinux::drivers::pci::PCI;
using cinux::net::ArpModule;
using cinux::net::EthAddr;
using cinux::net::IcmpModule;
using cinux::net::InDevice;
using cinux::net::Ipv4Module;
using cinux::net::kEtherTypeArp;
using cinux::net::kEtherTypeIpv4;
using cinux::net::kLoopbackAddr;
using cinux::net::kSlirpGateway;
using cinux::net::kSlirpGuest;
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

// ============================================================
// ping 10.0.2.2 over the real e1000 + QEMU SLIRP user-net.
// The L3 stack is proven on loopback; this test isolates the e1000 adapter +
// the SLIRP delivery path.  Skip (pass) when no NIC is attached.
// ============================================================

void test_ping_e1000() {
    PCI pci;
    pci.init();
    PCIDevice dev{};
    if (!pci.find_e1000(dev)) {
        cinux::lib::kprintf("[net] no NIC -- skipping e1000 ping test\n");
        return;  // counts as a pass when no e1000 is attached
    }

    // Static: E1000Controller + the adapter's RX/TX buffers are a few KB; keep
    // them off the 16 KB kernel stack.  Constructor-injected (NOT ::instance()) --
    // proves the adapter is decoupled from the driver singleton.
    static E1000Controller nic;
    if (!nic.init(dev).ok() || !nic.start_rx().ok() || !nic.start_tx().ok()) {
        TEST_ASSERT_TRUE(false);
        return;
    }
    static E1000NetDevice adapter(nic);
    static ArpModule      arp;
    static IcmpModule     icmp;
    static Ipv4Module     ipv4(icmp, &arp);
    static NetStack       stack;
    stack.add_protocol(kEtherTypeArp, arp);
    stack.add_protocol(kEtherTypeIpv4, ipv4);

    InDevice cfg{};
    EthAddr  our_mac{};
    adapter.mac(our_mac);
    cfg.hw      = our_mac;
    cfg.local   = kSlirpGuest;    // 10.0.2.15
    cfg.gateway = kSlirpGateway;  // 10.0.2.2 -- SLIRP answers ARP + ICMP echo
    TEST_ASSERT_TRUE(stack.attach(adapter, cfg));

    icmp.reset();
    const uint16_t pid = 0x1234;
    const uint16_t seq = 1;

    // Ping loop.  Iter 1: send_echo_request -> ipv4.send -> ARP resolve miss ->
    // ARP request sent, IP packet deferred.  Poll (sti+hlt lets QEMU's main loop
    // run + the LAPIC timer wakes us) -> SLIRP's ARP reply caches the gateway MAC.
    // Iter 2+: ARP hit -> ICMP echo goes out -> poll -> SLIRP's echo reply lands.
    // Reuses the F5-M6 批b-fix timing verbatim (NEVER a trap-loop "pump").
    for (uint32_t i = 0; i < 4000 && icmp.reply_count() == 0; ++i) {
        (void)icmp.send_echo_request(adapter, kSlirpGateway, pid, seq, ipv4, stack);
        for (uint32_t j = 0; j < 4; ++j) {
            stack.poll();
            if (icmp.reply_count() > 0) {
                break;
            }
            cinux::arch::irq_enable();
            cinux::arch::hlt();
            cinux::arch::irq_disable();
        }
    }

    TEST_ASSERT_TRUE(icmp.reply_count() >= 1);
    cinux::lib::kprintf("[net] e1000 ping 10.0.2.2: reply id=0x%04x seq=%u (%u replies)\n",
                        icmp.last_reply_id(), static_cast<unsigned>(icmp.last_reply_seq()),
                        static_cast<unsigned>(icmp.reply_count()));
}

}  // namespace test_net

extern "C" void run_net_tests() {
    TEST_SECTION("net");
    RUN_TEST(test_net::test_ping_loopback);
    RUN_TEST(test_net::test_ping_e1000);
    TEST_SUMMARY();
}
