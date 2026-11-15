/**
 * @file    program_seat.cpp
 * @brief   The one ladder every program image climbs into its world.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_loader
 * @copyright Copyright (c) 2026
 */

#include "kernel/loader/program_seat.hpp"

#include "kernel/arch/random.hpp"
#include "kernel/proc/proc_config.hpp"

namespace cinux::loader {

base::Result<ProgramSeat> SeatProgram(const unsigned char* image,
                                      unsigned long bytes, const fs::FsNode& node,
                                      const fs::FsView& view, mm::AddressSpace& space,
                                      mm::VmaStore& vmas, std::uint64_t base) {
    // The world wants the node's whole size, which the stat-capable
    // backends answer; the staging-only shapes fall back to the bytes
    // the caller could read.
    unsigned long long file_bytes = bytes;
    if (view.ops->stat != nullptr) {
        fs::FsStat      probed{};
        const base::Result<void> kStat = view.ops->stat(view.self, node, probed);
        if (kStat.ok()) {
            file_bytes = probed.size;
        }
    }
    const LoadWorld kWorld{
        .space         = space,
        .vmas          = vmas,
        .file          = node,
        .file_bytes    = file_bytes,
        .file_ops      = view.ops,
        .file_self     = view.self,
        .file_lifetime = view.lifetime,
        .base          = base,
    };
    EntryFacts facts{};
    if (!LoadElfIntoSpace(image, bytes, kWorld, facts)) {
        return base::KernelError::kOutOfMemory;
    }
    facts.page_bytes = cinux::arch::page::kSize;
    facts.platform   = kPlatformName;

    std::uint32_t gnu_stack_flags = 0;
    const bool    kStackExecutable =
        ReadGnuStackFlags(image, bytes, gnu_stack_flags) &&
        (gnu_stack_flags & kSegmentExec) != 0;

    SeedProcessLayout(vmas);

    // A dynamic image names its interpreter; a static one takes this
    // half as a no-op and keeps its own entry.
    char        interpreter_path[fs::kFsPathMax] = {};
    const char* interpreter = nullptr;
    if (ReadInterpreterPath(image, bytes, interpreter_path, sizeof(interpreter_path))) {
        interpreter = interpreter_path;
    }
    if (interpreter != nullptr && facts.program_headers == 0) {
        // A dynamic image must carry its header table: the interpreter
        // walks it to relocate the program.
        return base::KernelError::kNotExecutable;
    }
    const auto kInterpreter = LoadInterpreter(interpreter, space, vmas);
    if (!kInterpreter.ok()) {
        return kInterpreter.error();
    }
    facts.interpreter_base = kInterpreter.value().interpreter_base;
    if (kInterpreter.value().program_entry != 0) {
        facts.program_entry = kInterpreter.value().program_entry;
    }
    return ProgramSeat{.facts = facts, .stack_executable = kStackExecutable};
}

void DrawEntryRandom(unsigned char (&bytes)[16]) {
    for (unsigned long offset = 0; offset < sizeof(bytes); offset += sizeof(unsigned long long)) {
        unsigned long long word = 0;
        if (!arch::random::TryWord(word)) {
            word = arch::random::Word();
        }
        base::CopyBytes(bytes + offset, &word, sizeof(word));
    }
}

unsigned long long DrawEntryStackTop() {
    constexpr unsigned long long kStackSlideMask = 0x7FF'000;
    return proc::kUserProgramStackTop - (arch::random::Word() & kStackSlideMask);
}

}  // namespace cinux::loader
