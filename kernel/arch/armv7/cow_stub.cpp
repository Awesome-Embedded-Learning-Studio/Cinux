/**
 * @file    cow_stub.cpp
 * @brief   The armv7 lane has no copy-on-write yet; say so if it is asked.
 *
 * `mm`'s address-space teardown retires user frames through the COW
 * ledger. This lane never tears a space down, but the symbol must exist
 * for the link. Answering with a halt keeps a future caller from
 * silently leaking frames.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/halt.hpp"
#include "kernel/boot/print.hpp"

namespace cinux::mm::cow {

void ReleaseFrame(unsigned long long frame) {
    cinux::print::Println("[armv7] cow release of %X is not implemented", frame);
    cinux::arch::Halt();
}

}  // namespace cinux::mm::cow
