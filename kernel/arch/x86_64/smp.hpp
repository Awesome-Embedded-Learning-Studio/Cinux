/**
 * @file kernel/arch/x86_64/smp.hpp
 * @brief AP (Application Processor) boot interface (F4-M3 Phase 2)
 */

#pragma once

namespace cinux::arch {

/// Boot every Application Processor listed in the ACPI MADT (BSP-side).
///
/// Run once during init, after the scheduler and the APIC are up.  Each AP is
/// driven through the INIT-SIPI-SIPI sequence, reaches 64-bit long mode via the
/// trampoline at physical 0x8000, runs ap_main(), and then idles (it does not
/// run user tasks -- that is M4 multi-core scheduling).  No-op when there is
/// only one CPU.
void boot_aps();

}  // namespace cinux::arch
