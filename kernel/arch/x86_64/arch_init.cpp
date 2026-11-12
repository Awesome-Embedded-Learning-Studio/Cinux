#include "kernel/arch/init.hpp"

#include "kernel/arch/x86_64/entropy.hpp"
#include "kernel/arch/x86_64/ex_table.hpp"
#include "kernel/arch/x86_64/irq_stubs.hpp"
#include "kernel/arch/x86_64/cpuid.hpp"
#include "kernel/arch/x86_64/instructions.hpp"
#include "kernel/arch/x86_64/msr.hpp"
#include "kernel/arch/x86_64/per_cpu.hpp"
#include "kernel/arch/x86_64/pic.hpp"
#include "kernel/arch/x86_64/platform/acpi/acpi.hpp"
#include "kernel/arch/x86_64/platform/acpi/madt.hpp"
#include "kernel/arch/x86_64/platform/apic/local_apic.hpp"
#include "kernel/arch/x86_64/protections.hpp"
#include "kernel/arch/x86_64/registers.hpp"
#include "kernel/arch/x86_64/smp.hpp"
#include "kernel/arch/x86_64/tss.hpp"
#include "kernel/arch/x86_64/usermode.hpp"
#include "kernel/boot/boot_info.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::arch::init {

using print::Println;

void PrintArchBanner() {
    Println("[kern] 64-bit C++ world alive");
    Println("[kern] gdt+idt self-owned, sse on");
    Println("[kern] tss loaded tr=%X gs base %X efer %X", tss::ReadTaskRegister(),
            per_cpu::ReadGsBase(), ReadMsr(msr::kEfer));
    Println("[kern] kernel gs base %X", per_cpu::ReadKernelGsBase());
}

bool BringUpInterruptController(const cinux::boot::BootInfo& info) {
    static_cast<void>(info);
    const auto kTopology =
        driver::acpi::FindInterruptTopology(driver::acpi::FindRootPointer());
    if (!driver::LocalApic::self().init(kTopology.local_apic_address)) {
        return false;
    }
    Println("[apic] %u processor(s), local id %u, IOAPIC %x, overrides %u",
            kTopology.processor_count, driver::LocalApic::self().id(), kTopology.io_apic_address,
            kTopology.override_count);
    return true;
}

void RemapLegacyInterruptController() {
    Pic::self().remap();
}

void EnableFastPaths() {
    usermode::EnableFastSystemCalls();
    ex_table::ArmTable();
    syscall::uaccess::ArmWindows(HasSmap());
}

void SeedKernelRandom() {
    cinux::arch::SeedKernelRandom();
}

void EnableCoreProtections() {
    cinux::arch::EnableCoreProtections();
    Println("[kern] cr4 %X after core protections", ReadCr4());
}

void InstallInterruptStubs() {
    irq::InstallIrqStubs();
}

bool InstallInterruptDelivery() {
    return irq::InstallApicBackend();
}

void EnableInterrupts() {
    asm volatile("sti" : : : "memory");
}

bool BootSecondaryProcessors() {
    if (!smp::BootSecondaryCpus()) {
        return false;
    }
    Println("[smp] %u CPUs online", smp::OnlineCpuCount());
    for (unsigned int cpu = 0; cpu < smp::OnlineCpuCount(); ++cpu) {
        const auto& state = smp::Mechanisms(cpu);
        Println("[cpu %u] APIC %u online, LSTAR %llx, EFER %llx, CR4 %llx", state.cpu_id,
                state.apic_id, state.lstar, state.efer, state.cr4);
    }
    return true;
}

void EnableSecondaryDispatch() {
    smp::EnableSecondaryDispatch();
}

}  // namespace cinux::arch::init
