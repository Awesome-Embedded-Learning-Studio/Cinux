/**
 * @file    kmain.cpp
 * @brief   The armv7 bring-up entry: say hello, show the timer, wait.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/cpuid.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/heap_runtime.hpp"
#include "kernel/platform/bus.hpp"
#include "kernel/platform/clock.hpp"
#include "kernel/platform/firmware.hpp"
#include "kernel/platform/interrupt.hpp"
#include "kernel/platform/io.hpp"
#include "kernel/platform/ps2.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/arch/armv7/fdt.hpp"
#include "kernel/arch/armv7/firmware.hpp"
#include "kernel/arch/armv7/lcdif.hpp"
#include "kernel/drivers/usb/ehci.hpp"
#include "kernel/arch/armv7/selfcheck.hpp"
#include "kernel/arch/armv7/task_switch.hpp"
#include "kernel/boot/boot_info.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/armv7/gic.hpp"
#include "kernel/arch/armv7/mmu.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/instructions.hpp"
#include "kernel/drivers/usb/hcd.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"

namespace {

void PrintUnsigned(unsigned long long value) {
    char digits[21];
    int  count = 0;
    if (value == 0) {
        cinux::armv7::UartPutChar('0');
        return;
    }
    while (value != 0) {
        digits[count] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
        ++count;
    }
    while (count > 0) {
        --count;
        cinux::armv7::UartPutChar(digits[count]);
    }
}

void PrintHex(unsigned long long value) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    bool           started   = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned nibble = static_cast<unsigned>((value >> shift) & 0xFU);
        if (nibble != 0 || started || shift == 0) {
            cinux::armv7::UartPutChar(kDigits[nibble]);
            started = true;
        }
    }
}

}  // namespace

extern "C" [[noreturn]] void kmain() {
    cinux::armv7::UartInit();
    cinux::boot::BootInfo console_seat{};
    cinux::console::InitConsole(console_seat);
    cinux::armv7::UartWrite("\nCinux armv7: alive on i.MX6ULL\n");

    cinux::armv7::UartWrite("[armv7] page ");
    PrintUnsigned(cinux::arch::page::kSize);
    cinux::armv7::UartWrite(" bytes, timer: freq ");
    PrintUnsigned(cinux::armv7::TimerFrequency());
    cinux::armv7::UartWrite(" Hz, counter 0x");
    PrintHex(cinux::armv7::TimerCounter());
    cinux::armv7::UartWrite("\n");

    static_assert(cinux::arch::page::kLeafLevel == cinux::arch::page::WalkLevel::kL2,
                  "the bring-up image walks two levels");
    static_assert(cinux::arch::page::EntriesAt(cinux::arch::page::WalkLevel::kL1) == 4096,
                  "level 1 holds 4096 entries");
    static_assert(cinux::mm::kUserMmapTop < cinux::mm::kUserStackBottom,
                  "the user seats keep their order on a 32-bit address space");

    extern int _vectors[];
    asm volatile("mcr p15, 0, %0, c12, c0, 0" : : "r"(_vectors));  // VBAR
    asm volatile("isb");
    cinux::armv7::GicInit();
    cinux::armv7::TimerArmAfter(cinux::armv7::TimerFrequency() / 10U);
    cinux::armv7::TimerEnable();
    {
        constexpr unsigned long kDtbAddress = 0x81000000UL;
        cinux::armv7::firmware::SetDtb(kDtbAddress);
        cinux::armv7::MemoryRange range{};
        if (!cinux::armv7::ReadMemoryRange(kDtbAddress, range)) {
            cinux::armv7::UartWrite("[armv7] dtb missing or unreadable\n");
        } else {
            cinux::armv7::UartWrite("[armv7] dtb memory 0x");
            PrintHex(range.base);
            cinux::armv7::UartWrite(" + 0x");
            PrintHex(range.size);
            cinux::armv7::UartWrite("\n");

            // The firmware face: the same name the x86 lane reads from
            // fw_cfg answers from the tree this port booted with.
            char environment[64];
            if (cinux::armv7::firmware::ReadString("opt/cinux/init-env", environment)) {
                cinux::armv7::UartWrite("[armv7] firmware init-env ");
                cinux::armv7::UartWrite(environment);
                cinux::armv7::UartWrite("\n");
            } else {
                cinux::armv7::UartWrite("[armv7] firmware init-env missing\n");
            }

            cinux::boot::BootInfo info{};
            info.magic       = cinux::boot::kBootInfoMagic;
            info.version     = cinux::boot::kBootInfoVersion;
            info.struct_size = sizeof(cinux::boot::BootInfo);
            info.e820_count  = 1;
            info.e820[0]     = cinux::boot::E820Entry{.base = range.base,
                                                      .length = range.size,
                                                      .type   = cinux::boot::kE820Usable};
            // The kernel image ends at the last bank stack: the abort-mode
            // stack sits after the fiq stack, and everything above must stay
            // reserved or the PMM hands those frames to the kernel heap.
            extern unsigned char __abt_stack_top[];
            info.kernel_paddr    = 0x80000000UL;
            info.kernel_mem_size = reinterpret_cast<unsigned long>(__abt_stack_top) - 0x80000000UL;
            info.kernel_file_size = info.kernel_mem_size;
            info.kernel_entry     = 0x80000000UL;
            if (cinux::mm::Pmm::self().init(info)) {
                cinux::mm::BringUpAddressSpace(info);
                cinux::armv7::UartWrite("[armv7] mmu on\n");
                cinux::armv7::UartWrite(cinux::armv7::TwoLevelDemo()
                                            ? "[armv7] two-level page ok\n"
                                            : "[armv7] two-level page FAILED\n");
                cinux::armv7::UartWrite("[armv7] pmm free pages ");
                PrintUnsigned(cinux::mm::Pmm::self().free_page_count());
                cinux::armv7::UartWrite("\n");
                cinux::armv7::UartWrite(cinux::armv7::RunPmmSelfCheck()
                                            ? "[armv7] selfcheck pmm ok\n"
                                            : "[armv7] selfcheck pmm FAILED\n");
                cinux::print::Println("[armv7] console up, %u pages managed",
                                      cinux::mm::Pmm::self().managed_page_count());
                cinux::print::Println(cinux::armv7::RunHeapSelfCheck()
                                          ? "[armv7] selfcheck heap ok"
                                          : "[armv7] selfcheck heap FAILED");
                if (cinux::mm::BringUpHeap()) {
                    auto* const probe = new unsigned long(0x114514UL);
                    cinux::print::Println("[armv7] paged heap live, probe %X", *probe);
                    delete probe;
                    cinux::print::Println(cinux::armv7::RunMapSelfCheck()
                                              ? "[armv7] mapped page ok"
                                              : "[armv7] mapped page FAILED");
                    cinux::print::Println(cinux::armv7::RunContextSelfCheck()
                                              ? "[armv7] context switch ok"
                                              : "[armv7] context switch FAILED");
                    cinux::print::Println(cinux::armv7::RunPerCpuSelfCheck()
                                              ? "[armv7] per-cpu ok"
                                              : "[armv7] per-cpu FAILED");
                    {
                        // The identity probe exercises the port's MIDR
                        // decode on real hardware; QEMU's Cortex-A7
                        // answers implementer 0x41, part 0xC07.
                        const auto kIdentity = cinux::arch::ReadCpuIdentity(0);
                        cinux::print::Println("[armv7] cpu id %s %X",
                                              kIdentity.vendor[0] != '\0' ? kIdentity.vendor
                                                                          : "unknown",
                                              kIdentity.model);
                    }
                    cinux::print::Println(cinux::armv7::RunCowSelfCheck()
                                              ? "[armv7] cow ledger ok"
                                              : "[armv7] cow ledger FAILED");
                    cinux::print::Println(cinux::armv7::RunSpaceSelfCheck()
                                              ? "[armv7] space teardown ok"
                                              : "[armv7] space teardown FAILED");
                    cinux::print::Println(cinux::armv7::RunSharedSpaceSelfCheck()
                                              ? "[armv7] shared frame ok"
                                              : "[armv7] shared frame FAILED");
                    cinux::print::Println(cinux::armv7::RunSchedulerSelfCheck()
                                              ? "[armv7] sched roundtrip ok"
                                              : "[armv7] sched roundtrip FAILED");
                    {
                        const unsigned long kFrame = cinux::armv7::lcdif::InitFramebuffer();
                        cinux::print::Println("[armv7] lcdif 320x240 fb %X",
                                              kFrame != 0 ? kFrame : 0);
                    }
                    for (const unsigned long kBase : {0x02184000UL, 0x02184200UL}) {
                        if (cinux::driver::usb::EhciHost::self().init(kBase)) {
                            for (unsigned int port = 1;
                                 port <= cinux::driver::usb::EhciHost::self().max_ports() &&
                                 port <= 15; ++port) {
                                cinux::print::Println(
                                    "[armv7] ehci %X rev %X ports %u, port %u %s", kBase,
                                    cinux::driver::usb::EhciHost::self().revision(),
                                    cinux::driver::usb::EhciHost::self().max_ports(), port,
                                    cinux::driver::usb::EhciHost::self().port_connected(port)
                                        ? "attached"
                                        : "quiet");
                            }
                        }
                    }
                    cinux::print::Println(cinux::driver::usb::EhciHost::self().present()
                                              ? "[armv7] ehci host running"
                                              : "[armv7] ehci absent");
                    if (cinux::driver::usb::EhciHost::self().present()) {
                        for (unsigned int port = 1;
                             port <= cinux::driver::usb::EhciHost::self().max_ports(); ++port) {
                            if (cinux::driver::usb::EhciHost::self().port_taken(port) ||
                                !cinux::driver::usb::EhciHost::self().port_connected(port)) {
                                continue;
                            }
                            void* seat = cinux::driver::usb::EhciHost::self().address(port);
                            if (seat == nullptr) {
                                cinux::print::Println("[armv7] ehci seat refused %u",
                                                       cinux::driver::usb::EhciHost::self()
                                                                   .last_fail());
                                break;
                            }
                            unsigned char descriptor[18] = {};
                            if (cinux::driver::usb::EhciHost::self().control_read(
                                    seat, 0x0100, descriptor, sizeof(descriptor))) {
                                const unsigned int kVendor =
                                    descriptor[8] | (descriptor[9] << 8U);
                                const unsigned int kProduct =
                                    descriptor[10] | (descriptor[11] << 8U);
                                cinux::print::Println("[armv7] ehci device %04X:%04X seated",
                                                       kVendor, kProduct);
                            } else {
                                cinux::print::Println("[armv7] ehci descriptor read FAILED");
                            }
                            // The street below re-seats this port through
                            // the neutral walk, so hand the seat back.
                            cinux::driver::usb::PrimaryHost()->detach(seat);
                        }
                    }
                    // The whole neutral walk — enumerate, configure,
                    // arm — lives in the class driver now; this lane
                    // only asks for its seat and keeps the verdict.
                    cinux::print::Println(cinux::driver::UsbKeyboard::self().init()
                                              ? "[armv7] ehci keyboard street ok"
                                              : "[armv7] ehci keyboard street FAILED");
                    cinux::print::Println(cinux::armv7::RunForkSelfCheck()
                                              ? "[armv7] fork clone ok"
                                              : "[armv7] fork clone FAILED");
                    cinux::print::Println(cinux::armv7::RunTaskSelfCheck()
                                              ? "[armv7] two tasks ok"
                                              : "[armv7] two tasks FAILED");
                    // The bring-up task world is gone: the seated
                    // scheduler world is the one user-mode round trip.
                    const bool kSeatOk = cinux::armv7::RunSchedulerUserSelfCheck();
                    cinux::print::Println(kSeatOk ? "[armv7] sched user seat ok"
                                                  : "[armv7] sched user seat FAILED");
                    cinux::print::Println(kSeatOk ? "[armv7] user mode round trip ok"
                                                  : "[armv7] user mode round trip FAILED");
                    asm volatile("cpsie if" ::: "memory");
                    cinux::armv7::UartWrite("[armv7] irq on, timer armed\n");
                    const unsigned long long kWait =
                        static_cast<unsigned long long>(cinux::armv7::TimerFrequency()) / 5U;
                    const unsigned long long kStart = cinux::armv7::TimerCounter();
                    while (cinux::armv7::TimerCounter() - kStart < kWait) {
                        cinux::arch::CpuIdle();
                    }
                    cinux::armv7::UartWrite("[armv7] kernel fault probe\n");
                    // Deliberately unmapped: the abort reporter should name it.
                    *reinterpret_cast<volatile unsigned long*>(0x51000000UL) = 0xDEADUL;
                } else {
                    cinux::print::Println("[armv7] paged heap FAILED");
                }
            } else {
                cinux::armv7::UartWrite("[armv7] pmm refused init\n");
            }
        }
    }
    // The USB street drains on read through the class driver's poll
    // hook, the same face the x86 lane owns; nothing needs polling here.
    for (;;) {
        cinux::arch::CpuIdle();
    }
}
