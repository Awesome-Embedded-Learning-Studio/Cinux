/**
 * @file    init.hpp
 * @brief   The architecture's own bring-up steps, called by the neutral
 *          init sequence at the points where they belong.
 *
 * The init order is the kernel's policy and lives in kernel/init. What
 * each step does inside a machine (map the local interrupt controller,
 * remap the legacy one, arm the fast-syscall path, seed randomness,
 * turn protections on, install stubs, start the other processors) is the
 * port's business. Each function is one step of that order; a port fills
 * them in and the order survives.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::boot {
struct BootInfo;
}

namespace cinux::arch::init {

/// Prints the machine's own banner lines at the top of the kernel entry.
void PrintArchBanner();

/**
 * @brief         Maps and prepares this machine's interrupt controller.
 * @param[in]     info   The boot facts (firmware tables, CPU count).
 * @return        true when the controller answered; false halts init.
 */
bool BringUpInterruptController(const cinux::boot::BootInfo& info);

/// Reprograms whatever legacy controller the port has to stop it firing.
void RemapLegacyInterruptController();

/// Arms the port's fast user/kernel crossing and its fault windows.
void EnableFastPaths();

/// Seeds the kernel's random source from whatever this machine offers.
void SeedKernelRandom();

/// Turns on this machine's core memory protections.
void EnableCoreProtections();

/// Installs the port's interrupt entry stubs.
void InstallInterruptStubs();

/// Wires the controller's delivery path; false halts init.
bool InstallInterruptDelivery();

/// Opens the interrupt gate on this CPU.
void EnableInterrupts();

/// Starts the secondary processors; false halts init.
bool BootSecondaryProcessors();

/// Lets the other processors take dispatch once the boot CPU is settled.
void EnableSecondaryDispatch();

}  // namespace cinux::arch::init
