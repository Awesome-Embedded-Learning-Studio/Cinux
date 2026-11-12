/**
 * @file    user_mode.cpp
 * @brief   Two user tasks, each with its own table, yielding to each other.
 *
 * The svc stub traps a user task by saving its frame (r4-r11, user sp,
 * user pc, SPSR) into g_user_save_area. c_schedule_from_user copies that
 * frame into the running task, picks the next runnable one, points TTBR0
 * at its table, and hands its frame back; the stub restores it through an
 * exception return. Exiting marks the task done and either starts the
 * other or resumes the kernel.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.6
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <cstdint>

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space.hpp"
#include "kernel/arch/isr.hpp"
#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/errno.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/arch/task_switch.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/errno.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/armv7/clone_space.hpp"
#include "kernel/arch/armv7/shared_map.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

using FaultRepair = bool (*)(unsigned long address, unsigned long status);
void InstallFaultRepair(FaultRepair hook);

extern "C" unsigned char __user_text_start[];
extern "C" unsigned char __user_text_end[];

namespace {

constexpr unsigned long kTaskACode  = 0x40001000UL;
constexpr unsigned long kTaskAStack = 0x40004000UL;  // code gets the pages before it
constexpr unsigned long kTaskBCode  = 0x40003000UL;
constexpr unsigned long kTaskBStack = 0x40004000UL;

enum class TaskState : unsigned char { kRunnable, kBlocked, kDone };


/// What a descriptor points at. Files will be a third kind.
enum class FdKind : unsigned long {
    kConsoleIn  = 0,
    kConsoleOut = 1,
    kFile       = 2,
};

struct FdEntry {
    bool                 open   = false;
    FdKind               kind   = FdKind::kConsoleIn;
    unsigned long        offset = 0;
    const unsigned char* data     = nullptr;  // file contents, if any
    unsigned long        size     = 0;
    bool                 writable = false;
};

constexpr unsigned long kMaxFds = 8;

/// The one file this port has without a filesystem: enough to prove that
/// open, read and close walk real bytes.
unsigned char g_scratch_file[64];  // writable, zeroed at boot
unsigned long g_scratch_len = 0;

constexpr unsigned char kHelloFile[] = "hello\n";
constexpr unsigned long kHelloSize   = sizeof(kHelloFile) - 1;
constexpr unsigned char kMotdFile[]  = "hi there\n";
constexpr unsigned long kMotdSize    = sizeof(kMotdFile) - 1;

/// The read-only files this port knows by name.
struct BuiltinFile {
    const char*          name;
    unsigned long        name_length;
    const unsigned char* data;
    unsigned long        size;
};

constexpr BuiltinFile kBuiltinFiles[] = {
    {"/hello", 6, kHelloFile, kHelloSize},
    {"/motd", 5, kMotdFile, kMotdSize},
};

struct UserTask {
    cinux::arch::UserTrap    trap;
    cinux::mm::AddressSpace* space;
    TaskState                state;
    unsigned long            exit_status;
    unsigned long            wait_target;   // valid while blocked
    unsigned long            wait_mask;     // bit per awaited task
    unsigned long            wait_deadline; // ticks; valid while blocked
    unsigned long            break_top;     // grow-only user heap break
    FdEntry                  fds[kMaxFds];  // small table, one slot per code
    unsigned long            fault_handler; // user fault handler, 0 if none
    unsigned long            parent;        // who forked this task
    unsigned long            mmap_top;      // grow-only anonymous window
    bool                     in_use;         // this slot owns a space
    bool                     read_pending;   // a read is parked, waiting for input
    unsigned long            read_buf;       // its destination
    unsigned long            read_count;     // how many bytes it asked for
};

constexpr unsigned long kTaskCount   = 8;
constexpr unsigned long kInitialTasks = 4;
constexpr unsigned long kNoParent = 0xFFUL;  // reported as "no parent"
constexpr unsigned long kBrkBase     = 0x4000C000UL;
constexpr unsigned long kBrkLimit    = 0x40010000UL;
constexpr unsigned long kMmapBase    = 0x40010000UL;
constexpr unsigned long kMmapLimit   = 0x40100000UL;
constexpr unsigned long kReadTicks   = 10;  // default wait for input, one second
constexpr unsigned long kWaitTicks  = 20;  // default deadline when the caller passes 0

UserTask                g_tasks[kTaskCount];
cinux::arch::CpuContext g_main;
cinux::arch::CpuContext g_scratch;
unsigned long           g_kernel_root = 0;
unsigned long           g_current     = 0;
bool                    g_running     = false;
unsigned long long      g_shared_frame = 0;
unsigned long           g_tick_now     = 0;  // refreshed by the IRQ path

long user_syscall0(unsigned long number) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = 0;
    register unsigned long r1 asm("r1") = 0;
    register unsigned long r2 asm("r2") = 0;
    register unsigned long r3 asm("r3") = 0;
    // A trapping svc keeps no promise about r0-r3; tell the compiler.
    asm volatile("svc #0"
                 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                 : "r"(r7)
                 : "cc", "memory", "ip", "lr");
    return static_cast<long>(r0);
}

long user_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1,
                   unsigned long arg2) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = arg0;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;
    register unsigned long r3 asm("r3") = 0;
    asm volatile("svc #0"
                 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                 : "r"(r7)
                 : "cc", "memory", "ip", "lr");
    return static_cast<long>(r0);
}

void user_write(const char* text, unsigned long length) {
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kWrite, 1,
                                    static_cast<unsigned long>(
                                        reinterpret_cast<std::uintptr_t>(text)),
                                    length));
}

extern "C" void user_entry_stub();

/// Runs on a user page; reaches the kernel only through svc.
/// Runs in user mode after a fault when the task registered a handler.
extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_fault_handler_c(
    unsigned long /*address*/, unsigned long /*code*/);

extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_fault_handler_c(
    unsigned long /*address*/, unsigned long /*code*/) {
    char          note[16];
    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
    NOTE('h'); NOTE('a'); NOTE('n'); NOTE('d'); NOTE('l'); NOTE('e'); NOTE('r');
    NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
    user_write(note, note_length);
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 9, 0, 0));
    for (;;) {
    }
}

extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_program_c(
    unsigned long task_id) {
    char          buffer[64];
    unsigned long length = 0;
#define PUSH_CHAR(c) buffer[length++] = (c)
    PUSH_CHAR('0' + static_cast<char>(task_id));
    PUSH_CHAR(':');
    PUSH_CHAR('?');
    PUSH_CHAR('['); PUSH_CHAR('a'); PUSH_CHAR('r'); PUSH_CHAR('m'); PUSH_CHAR('v');
    PUSH_CHAR('7'); PUSH_CHAR(']'); PUSH_CHAR(' '); PUSH_CHAR('u'); PUSH_CHAR('s');
    PUSH_CHAR('e'); PUSH_CHAR('r'); PUSH_CHAR(' '); PUSH_CHAR('w'); PUSH_CHAR('r');
    PUSH_CHAR('i'); PUSH_CHAR('t'); PUSH_CHAR('e'); PUSH_CHAR('\n');
#undef PUSH_CHAR
    // Write this task's own value into the shared page; the first write
    // takes a copy-on-write fault. It is read back only after the loop,
    // by which time preemption has let the other task write its own value
    // into the same address.
    constexpr unsigned long kSharedVa = 0x40008000UL;
    const unsigned long     kValue    = 0x1000UL + task_id;
    *reinterpret_cast<volatile unsigned long*>(kSharedVa) = kValue;

    // A pointer into the kernel's own memory must be refused, not read.
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kWrite, 1, 0x80000000UL, 8));
    // Task 0 finishes early so its wait really has to block.
    const unsigned long kRounds = task_id == 0 ? 2UL : 20UL;
    for (unsigned long round = 0; round < kRounds; ++round) {
        buffer[2] = static_cast<char>('0' + (round % 10));
        user_write(buffer, length);
        for (volatile unsigned long spin = 0; spin < 6000000UL; ++spin) {
        }

    }
    {
        char          note[32];
        unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
        NOTE('t'); NOTE('a'); NOTE('s'); NOTE('k'); NOTE(' ');
        NOTE(static_cast<char>('0' + task_id));
        NOTE(' ');
        if (*reinterpret_cast<volatile unsigned long*>(kSharedVa) == kValue) {
            NOTE('k'); NOTE('e'); NOTE('e'); NOTE('p'); NOTE('s'); NOTE(' ');
            NOTE('i'); NOTE('t'); NOTE('s'); NOTE(' '); NOTE('p'); NOTE('a');
            NOTE('g'); NOTE('e'); NOTE('\n');
        } else {
            NOTE('l'); NOTE('o'); NOTE('s'); NOTE('t'); NOTE(' '); NOTE('i');
            NOTE('t'); NOTE('s'); NOTE(' '); NOTE('p'); NOTE('a'); NOTE('g');
            NOTE('e'); NOTE('\n');
        }
#undef NOTE
        user_write(note, note_length);
    }
    if (task_id == 0) {
        const long kYielded = user_syscall3(cinux::armv7::syscall_nr::kSchedYield, 0, 0, 0);
        if (kYielded == 0) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('y'); NOTE('i'); NOTE('e'); NOTE('l'); NOTE('d'); NOTE(' ');
            NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        const long kBadFlags = user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096, 0, 2);
        if (kBadFlags == static_cast<long>(cinux::armv7::errno::kInvalid)) {
            char          note[24];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('f'); NOTE('l'); NOTE('a'); NOTE('g'); NOTE('s'); NOTE(' ');
            NOTE('r'); NOTE('e'); NOTE('f'); NOTE('u'); NOTE('s'); NOTE('e');
            NOTE('d'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        constexpr unsigned long kRoHint = 0x40030000UL;
        const long kRo = user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096, kRoHint, 1);
        if (kRo == static_cast<long>(kRoHint)) {
            auto* const kSpot = reinterpret_cast<volatile unsigned long*>(kRoHint);
            *kSpot            = 0x5EEDUL;
            if (*kSpot == 0x5EEDUL) {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('r'); NOTE('o'); NOTE(' '); NOTE('c'); NOTE('o'); NOTE('p');
                NOTE('y'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
        }
    }
    if (task_id == 0) {
        constexpr unsigned long kHint = 0x40020000UL;
        const long kHinted =
            user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096, kHint, 0);
        if (kHinted == static_cast<long>(kHint)) {
            auto* const kSpot = reinterpret_cast<volatile unsigned long*>(kHint);
            *kSpot            = 0xF00DUL;
            const bool kFirstWrite = (*kSpot == 0xF00DUL);
            static_cast<void>(
                user_syscall3(cinux::armv7::syscall_nr::kMunmap, kHint, 4096, 0));
            const long kAgain =
                user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096, kHint, 0);
            const long kTaken = user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096,
                                              kHint, 0);
            if (kFirstWrite && kAgain == static_cast<long>(kHint) && kTaken == static_cast<long>(cinux::armv7::errno::kNoMem)) {
                *kSpot = 0xCAFEUL;
                if (*kSpot == 0xCAFEUL) {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('m'); NOTE('m'); NOTE('a'); NOTE('p'); NOTE(' '); NOTE('h');
                    NOTE('i'); NOTE('n'); NOTE('t'); NOTE(' '); NOTE('o'); NOTE('k');
                    NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
            }
        }
    }
    if (task_id == 0) {
        const long kMapped = user_syscall3(cinux::armv7::syscall_nr::kMmap, 8192, 0, 0);
        if (kMapped > 0) {
            auto* const kSpot = reinterpret_cast<volatile unsigned long*>(
                static_cast<unsigned long>(kMapped));
            *kSpot = 0xD00DUL;
            if (*kSpot == 0xD00DUL) {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('m'); NOTE('m'); NOTE('a'); NOTE('p'); NOTE(' '); NOTE('o');
                NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
        }
    }
    if (task_id == 0) {
        const unsigned long kWant = 0x4000E000UL;  // two pages past the base
        const long          kGot  = user_syscall3(cinux::armv7::syscall_nr::kBrk, kWant, 0, 0);
        if (static_cast<unsigned long>(kGot) >= kWant) {
            auto* const kLast = reinterpret_cast<volatile unsigned long*>(0x4000DFFCUL);
            *kLast            = 0xBEEFUL;
            if (*kLast == 0xBEEFUL) {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('b'); NOTE('r'); NOTE('k'); NOTE(' '); NOTE('o'); NOTE('k');
                NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
        }
    }
    if (task_id == 0) {
        // Filled element by element: an initializer-only array turns into
        // a constant in kernel .rodata, which user mode cannot read.
        char name[8];
        name[0] = '/'; name[1] = 'h'; name[2] = 'e'; name[3] = 'l';
        name[4] = 'l'; name[5] = 'o'; name[6] = 0;   name[7] = 0;
        const long kFd = user_syscall3(
            cinux::armv7::syscall_nr::kOpen,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(name)), 0, 0);
        if (kFd >= 3) {
            char buf[8];
            for (unsigned long index = 0; index < 8; ++index) {
                buf[index] = 0;
            }
            const long kBytes = user_syscall3(
                cinux::armv7::syscall_nr::kRead,
                static_cast<unsigned long>(kFd),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(buf)), 6);
            const bool kSawIt = kBytes == 6 && buf[0] == 'h' && buf[1] == 'e' &&
                                buf[2] == 'l' && buf[3] == 'l' && buf[4] == 'o';
            if (kSawIt) {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('f'); NOTE('i'); NOTE('l'); NOTE('e'); NOTE(' '); NOTE('o');
                NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            char       half[8];
            for (unsigned long index = 0; index < 8; ++index) {
                half[index] = 0;
            }
            static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kLseek,
                                            static_cast<unsigned long>(kFd), 0, 0));
            const long kPart = user_syscall3(
                cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kFd),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(half)), 3);
            const long kBack = user_syscall3(
                cinux::armv7::syscall_nr::kLseek, static_cast<unsigned long>(kFd),
                0, 0);
            const long kAgain = user_syscall3(
                cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kFd),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(buf)), 6);
            if (kPart == 3 && kBack == 0 && kAgain == 6 && buf[0] == 'h' &&
                buf[5] == '\n') {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('l'); NOTE('s'); NOTE('e'); NOTE('e'); NOTE('k'); NOTE(' ');
                NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            char oneByte[2];
            oneByte[0] = 'z';
            oneByte[1] = 0;
            const long kRoWrite = user_syscall3(
                cinux::armv7::syscall_nr::kWrite, static_cast<unsigned long>(kFd),
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(oneByte)), 1);
            if (kRoWrite == static_cast<long>(cinux::armv7::errno::kBadFd)) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('r'); NOTE('o'); NOTE(' '); NOTE('w'); NOTE('r'); NOTE('i');
                NOTE('t'); NOTE('e'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                            static_cast<unsigned long>(kFd), 0, 0));
        }
        {
            char motd[8];
            motd[0] = '/'; motd[1] = 'm'; motd[2] = 'o'; motd[3] = 't';
            motd[4] = 'd'; motd[5] = 0;   motd[6] = 0;   motd[7] = 0;
            const long kMotdFd = user_syscall3(
                cinux::armv7::syscall_nr::kOpen,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(motd)), 0, 0);
            if (kMotdFd >= 3) {
                char back[12];
                for (unsigned long index = 0; index < 12; ++index) {
                    back[index] = 0;
                }
                const long kRead = user_syscall3(
                    cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kMotdFd),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(back)), 9);
                if (kRead == 9 && back[0] == 'h' && back[1] == 'i' && back[2] == ' ' &&
                    back[3] == 't' && back[4] == 'h' && back[5] == 'e' &&
                    back[6] == 'r' && back[7] == 'e' && back[8] == '\n') {
                    char          note[16];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('m'); NOTE('o'); NOTE('t'); NOTE('d'); NOTE(' ');
                    NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                                static_cast<unsigned long>(kMotdFd), 0, 0));
            }
        }
        {
            // Writable file, with every step reported.
            char scratch[10];
            scratch[0] = '/'; scratch[1] = 's'; scratch[2] = 'c'; scratch[3] = 'r';
            scratch[4] = 'a'; scratch[5] = 't'; scratch[6] = 'c'; scratch[7] = 'h';
            scratch[8] = 0;   scratch[9] = 0;
            const long kScrFd = user_syscall3(
                cinux::armv7::syscall_nr::kOpen,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(scratch)), 0, 0);
            if (kScrFd < 3) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('s'); NOTE('c'); NOTE('r'); NOTE(' '); NOTE('o'); NOTE('p');
                NOTE('e'); NOTE('n'); NOTE(' '); NOTE('b'); NOTE('a'); NOTE('d');
                NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            } else {
                char payload[6];
                payload[0] = 'a'; payload[1] = 'b'; payload[2] = 'c';
                payload[3] = '1'; payload[4] = '2'; payload[5] = '3';
                const long kWrote = user_syscall3(
                    cinux::armv7::syscall_nr::kWrite, static_cast<unsigned long>(kScrFd),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(payload)), 6);
                if (kWrote != 6) {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('s'); NOTE('c'); NOTE('r'); NOTE(' '); NOTE('w'); NOTE('r');
                    NOTE('i'); NOTE('t'); NOTE('e'); NOTE(' '); NOTE('b'); NOTE('a');
                    NOTE('d'); NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kLseek,
                                                static_cast<unsigned long>(kScrFd), 0, 0));
                char back[6];
                for (unsigned long index = 0; index < 6; ++index) {
                    back[index] = 0;
                }
                const long kBackBytes = user_syscall3(
                    cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kScrFd),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(back)), 6);
                const bool kOk = kBackBytes == 6 && back[0] == 'a' && back[1] == 'b' &&
                                 back[2] == 'c' && back[3] == '1' && back[4] == '2' &&
                                 back[5] == '3';
                {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    if (kOk) {
                        NOTE('w'); NOTE('r'); NOTE('i'); NOTE('t'); NOTE('e'); NOTE('f');
                        NOTE('i'); NOTE('l'); NOTE('e'); NOTE(' '); NOTE('o'); NOTE('k');
                    } else {
                        NOTE('w'); NOTE('r'); NOTE('i'); NOTE('t'); NOTE('e'); NOTE('f');
                        NOTE('i'); NOTE('l'); NOTE('e'); NOTE(' '); NOTE('b'); NOTE('a');
                        NOTE('d');
                    }
                    NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                {
                    // Append: seek to the end, write two more bytes.
                    const long kEnd = user_syscall3(
                        cinux::armv7::syscall_nr::kLseek,
                        static_cast<unsigned long>(kScrFd), 0, 2);
                    char tail[2];
                    tail[0] = 'X';
                    tail[1] = 'Y';
                    const long kAppended = user_syscall3(
                        cinux::armv7::syscall_nr::kWrite,
                        static_cast<unsigned long>(kScrFd),
                        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(tail)), 2);
                    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kLseek,
                                                    static_cast<unsigned long>(kScrFd), 0, 0));
                    char whole[12];
                    for (unsigned long index = 0; index < 12; ++index) {
                        whole[index] = 0;
                    }
                    const long kWhole = user_syscall3(
                        cinux::armv7::syscall_nr::kRead,
                        static_cast<unsigned long>(kScrFd),
                        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(whole)), 10);
                    if (kEnd == 6 && kAppended == 2 && kWhole == 8 && whole[6] == 'X' &&
                        whole[7] == 'Y') {
                        char          note[16];
                        unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                        NOTE('a'); NOTE('p'); NOTE('p'); NOTE('e'); NOTE('n'); NOTE('d');
                        NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                        user_write(note, note_length);
                    }
                }
                const long kBadSeek = user_syscall3(
                    cinux::armv7::syscall_nr::kLseek, static_cast<unsigned long>(kScrFd),
                    static_cast<unsigned long>(-1), 0);
                if (kBadSeek == static_cast<long>(cinux::armv7::errno::kInvalid)) {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('l'); NOTE('s'); NOTE('e'); NOTE('e'); NOTE('k'); NOTE(' ');
                    NOTE('b'); NOTE('a'); NOTE('d'); NOTE(' '); NOTE('o'); NOTE('k');
                    NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                const long kBadRead = user_syscall3(
                    cinux::armv7::syscall_nr::kRead, 0, 0x80000000UL, 4);
                if (kBadRead == static_cast<long>(cinux::armv7::errno::kFault)) {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('r'); NOTE('e'); NOTE('a'); NOTE('d'); NOTE(' ');
                    NOTE('b'); NOTE('a'); NOTE('d'); NOTE(' '); NOTE('o'); NOTE('k');
                    NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                                static_cast<unsigned long>(kScrFd), 0, 0));
            }
        }
        {
            char second[8];
            second[0] = '/'; second[1] = 'h'; second[2] = 'e'; second[3] = 'l';
            second[4] = 'l'; second[5] = 'o'; second[6] = 0;   second[7] = 0;
            const long kA = user_syscall3(
                cinux::armv7::syscall_nr::kOpen,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(second)), 0, 0);
            const long kB = user_syscall3(
                cinux::armv7::syscall_nr::kOpen,
                static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(second)), 0, 0);
            if (kA >= 3 && kB >= 3 && kA != kB) {
                char pa[4] = {0, 0, 0, 0};
                char pb[4] = {0, 0, 0, 0};
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kLseek,
                                                static_cast<unsigned long>(kA), 0, 0));
                static_cast<void>(user_syscall3(
                    cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kA),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(pa)), 2));
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kLseek,
                                                static_cast<unsigned long>(kB), 3, 0));
                static_cast<void>(user_syscall3(
                    cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kB),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(pb)), 3));
                static_cast<void>(user_syscall3(
                    cinux::armv7::syscall_nr::kRead, static_cast<unsigned long>(kA),
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&pa[2])), 1));
                if (pa[0] == 'h' && pa[1] == 'e' && pa[2] == 'l' && pb[0] == 'l' &&
                    pb[1] == 'o' && pb[2] == '\n') {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('m'); NOTE('u'); NOTE('l'); NOTE('t'); NOTE('i'); NOTE(' ');
                    NOTE('o'); NOTE('p'); NOTE('e'); NOTE('n'); NOTE(' '); NOTE('o');
                    NOTE('k'); NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                }
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                                static_cast<unsigned long>(kA), 0, 0));
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                                static_cast<unsigned long>(kB), 0, 0));
            }
            long          last  = 0;
            unsigned long held  = 0;
            long          kept[6] = {0, 0, 0, 0, 0, 0};
            for (unsigned long round = 0; round < 6; ++round) {
                last = user_syscall3(
                    cinux::armv7::syscall_nr::kOpen,
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(second)), 0, 0);
                if (last < 0) {
                    break;
                }
                kept[held] = last;
                ++held;
            }
            for (unsigned long index = 0; index < held; ++index) {
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose,
                                                static_cast<unsigned long>(kept[index]), 0, 0));
            }
            if (last == static_cast<long>(cinux::armv7::errno::kNoMem)) {
                char          note[16];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('f'); NOTE('u'); NOTE('l'); NOTE('l'); NOTE(' '); NOTE('o');
                NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
        }
        char missing[4];
        missing[0] = '/'; missing[1] = 'n'; missing[2] = 0; missing[3] = 0;
        const long kMissing   = user_syscall3(
            cinux::armv7::syscall_nr::kOpen,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(missing)), 0, 0);
        if (kMissing == static_cast<long>(cinux::armv7::errno::kNoEnt)) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('e'); NOTE('n'); NOTE('o'); NOTE('e'); NOTE('n'); NOTE('t'); NOTE(' ');
            NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        char probe[4] = {'x', '\n', 0, 0};
        // Past the table: refused like a closed slot, not a crash.
        const long kOob = user_syscall3(
            cinux::armv7::syscall_nr::kWrite, 8,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(probe)), 1);
        if (kOob == static_cast<long>(cinux::armv7::errno::kBadFd)) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('f'); NOTE('d'); NOTE(' '); NOTE('r'); NOTE('a'); NOTE('n');
            NOTE('g'); NOTE('e'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
        static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kClose, 2, 0, 0));
        const long kRefused = user_syscall3(
            cinux::armv7::syscall_nr::kWrite, 2,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(probe)), 1);
        if (kRefused == static_cast<long>(cinux::armv7::errno::kBadFd)) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('e'); NOTE('b'); NOTE('a'); NOTE('d'); NOTE('f'); NOTE(' ');
            NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    {
        // No table entry: the dispatcher must refuse it, not guess.
        const long kUnknown = user_syscall3(0xFFFFUL, 0, 0, 0);
        if (kUnknown == -1) {
            char          note[24];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('u'); NOTE('n'); NOTE('k'); NOTE('n'); NOTE('o'); NOTE('w');
            NOTE('n'); NOTE(' '); NOTE('r'); NOTE('e'); NOTE('f'); NOTE('u');
            NOTE('s'); NOTE('e'); NOTE('d'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    {
        const long kPid = user_syscall3(cinux::armv7::syscall_nr::kGetPid, 0, 0, 0);
        if (static_cast<unsigned long>(kPid) == task_id) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('p'); NOTE('i'); NOTE('d'); NOTE(' ');
            NOTE(static_cast<char>('0' + task_id)); NOTE(' '); NOTE('o'); NOTE('k');
            NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        const long kBefore = user_syscall3(cinux::armv7::syscall_nr::kGetTicks, 0, 0, 0);
        const long kSlept  = user_syscall3(cinux::armv7::syscall_nr::kSleep, 3, 0, 0);
        const long kAfter  = user_syscall3(cinux::armv7::syscall_nr::kGetTicks, 0, 0, 0);
        if (kSlept == 0 && kAfter > kBefore) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('t'); NOTE('i'); NOTE('c'); NOTE('k'); NOTE('s'); NOTE(' ');
            NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
        if (kSlept == 0) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('s'); NOTE('l'); NOTE('e'); NOTE('p'); NOTE('t'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        // Waiting on itself can never be satisfied: it must time out.
        unsigned long status = 0;
        const long    kSelf  = user_syscall3(
            cinux::armv7::syscall_nr::kWait, 1UL,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&status)), 5);
        if (kSelf == -2) {
            char          note[32];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('s'); NOTE('e'); NOTE('l'); NOTE('f'); NOTE(' ');
            NOTE('w'); NOTE('a'); NOTE('i'); NOTE('t'); NOTE(' ');
            NOTE('t'); NOTE('i'); NOTE('m'); NOTE('e'); NOTE('d'); NOTE(' ');
            NOTE('o'); NOTE('u'); NOTE('t'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0 || task_id == 3) {
        // Task 0 reaps 1 then 2; task 3 only reaps 2, so a wake aimed at 1
        // must leave it asleep.
        unsigned long status = 0;
        unsigned long seen[2] = {0UL, 0UL};
        const unsigned long kTargets[] = {1UL, 2UL};
        const unsigned long kCount     = task_id == 0 ? 2UL : 1UL;
        for (unsigned long index = 0; index < kCount; ++index) {
            const unsigned long kTarget =
                task_id == 0 ? kTargets[index] : kTargets[1];
            for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                // A long deadline: the waiter must still be parked when
                // the target ends, so the wake is by target, not by timeout.
                const long kResult = user_syscall3(
                    cinux::armv7::syscall_nr::kWait, 1UL << kTarget,
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&status)), 200);
                if (kResult >= 0) {
                    seen[task_id == 0 ? index : 0] = status;
                    break;
                }
            }
        }
        if (task_id == 0 && seen[0] == 0xDEADUL && seen[1] == 9UL) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('s'); NOTE('t'); NOTE('a'); NOTE('t'); NOTE('u'); NOTE('s');
            NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    {
        // Inside the user window but never mapped: the repair hook fills it
        // and the store is retried on the new page.
        auto* const kProbe = reinterpret_cast<volatile unsigned long*>(0x40009000UL);
        *kProbe            = 0xC0FFEEUL;
    }
    {
        char       line[16];
        const long kReceived = user_syscall3(
            cinux::armv7::syscall_nr::kRead, 0,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(line)), sizeof(line));
        static_cast<void>(kReceived);
    }
    if (task_id == 0) {
        // A page both sides can see before the clone: each child's write
        // must get a private copy, the parent's must stay as it was.
        constexpr unsigned long kSharedVa = 0x40040000UL;
        static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kMmap, 4096, kSharedVa, 0));
        auto* const kSharedPage = reinterpret_cast<volatile unsigned long*>(kSharedVa);
        *kSharedPage            = 0x1111UL;
        // The first child finishes before the second fork: otherwise it
        // would execute that fork too and grow a grandchild.
        const long kFirst = user_syscall0(cinux::armv7::syscall_nr::kFork);
        if (kFirst == 0) {
            *kSharedPage = 0x2222UL;
            // Two things are inherited rather than re-created: the open
            // descriptors and the heap break.
            char probe = 'x';
            const long kClosedFd =
                user_syscall3(cinux::armv7::syscall_nr::kWrite, 2,
                              static_cast<unsigned long>(
                                  reinterpret_cast<std::uintptr_t>(&probe)),
                              1);
            if (kClosedFd == static_cast<long>(cinux::armv7::errno::kBadFd)) {
                char          note[32];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('f'); NOTE('d'); NOTE(' '); NOTE('i'); NOTE('n'); NOTE('h');
                NOTE('e'); NOTE('r'); NOTE('i'); NOTE('t'); NOTE('e'); NOTE('d');
                NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            const long kBreak = user_syscall3(cinux::armv7::syscall_nr::kBrk, 0, 0, 0);
            if (static_cast<unsigned long>(kBreak) == 0x4000E000UL) {
                char          note[32];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('b'); NOTE('r'); NOTE('k'); NOTE(' '); NOTE('i'); NOTE('n');
                NOTE('h'); NOTE('e'); NOTE('r'); NOTE('i'); NOTE('t'); NOTE('e');
                NOTE('d'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            const long kPpid = user_syscall3(cinux::armv7::syscall_nr::kGetPpid, 0, 0, 0);
            if (kPpid == 0) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('p'); NOTE('p'); NOTE('i'); NOTE('d'); NOTE(' ');
                NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            {
                const long kGrand = user_syscall0(cinux::armv7::syscall_nr::kFork);
                if (kGrand == 0) {
                    char          note[24];
                    unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                    NOTE('g'); NOTE('r'); NOTE('a'); NOTE('n'); NOTE('d'); NOTE('c');
                    NOTE('h'); NOTE('i'); NOTE('l'); NOTE('d'); NOTE(' '); NOTE('o');
                    NOTE('k'); NOTE('\n');
#undef NOTE
                    user_write(note, note_length);
                    static_cast<void>(
                        user_syscall3(cinux::armv7::syscall_nr::kExit, 4, 0, 0));
                    for (;;) {
                    }
                }
                unsigned long grand_status = 0;
                for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                    const long kResult = user_syscall3(
                        cinux::armv7::syscall_nr::kWait,
                        1UL << static_cast<unsigned long>(kGrand),
                        static_cast<unsigned long>(
                            reinterpret_cast<std::uintptr_t>(&grand_status)),
                        200);
                    if (kResult >= 0) {
                        if (grand_status == 4UL) {
                            char          note[24];
                            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                            NOTE('t'); NOTE('w'); NOTE('o'); NOTE(' '); NOTE('l');
                            NOTE('e'); NOTE('v'); NOTE('e'); NOTE('l'); NOTE('s');
                            NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                            user_write(note, note_length);
                        }
                        break;
                    }
                }
            }
            if (*kSharedPage == 0x2222UL) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('c'); NOTE('h'); NOTE('i'); NOTE('l'); NOTE('d'); NOTE(' ');
                NOTE('o'); NOTE('n'); NOTE('e'); NOTE(' '); NOTE('o'); NOTE('k');
                NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 3, 0, 0));
            for (;;) {
            }
        }
        const long kSecond = user_syscall0(cinux::armv7::syscall_nr::kFork);
        if (kSecond == 0) {
            *kSharedPage = 0x3333UL;
            if (*kSharedPage == 0x3333UL) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('c'); NOTE('h'); NOTE('i'); NOTE('l'); NOTE('d'); NOTE(' ');
                NOTE('t'); NOTE('w'); NOTE('o'); NOTE(' '); NOTE('o'); NOTE('k');
                NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
            static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 3, 0, 0));
            for (;;) {
            }
        }
        unsigned long status = 0;
        bool          reaped = true;
        const unsigned long kChildren[2] = {static_cast<unsigned long>(kSecond),
                                            static_cast<unsigned long>(kFirst)};
        for (unsigned long index = 0; index < 2; ++index) {
            for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                const long kResult = user_syscall3(
                    cinux::armv7::syscall_nr::kWait, 1UL << kChildren[index],
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&status)), 200);
                if (kResult >= 0) {
                    reaped = reaped && (status == 3UL);
                    break;
                }
            }
        }
        if (reaped) {
            const unsigned long kHandler =
                kTaskACode + (reinterpret_cast<std::uintptr_t>(&user_fault_handler_c) -
                              reinterpret_cast<std::uintptr_t>(__user_text_start));
            static_cast<void>(
                user_syscall3(cinux::armv7::syscall_nr::kSignal, 11, kHandler, 0));
            const long kThird = user_syscall0(cinux::armv7::syscall_nr::kFork);
            if (kThird == 0) {
                // The parent registered a fault handler before this fork:
                // the child inherits it and must land in it.
                static_cast<void>(*reinterpret_cast<volatile unsigned long*>(
                    0x51000010UL));
                static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 3, 0, 0));
                for (;;) {
                }
            }
            unsigned long reuse_status = 0;
            for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                const long kResult = user_syscall3(
                    cinux::armv7::syscall_nr::kWait,
                    1UL << static_cast<unsigned long>(kThird),
                    static_cast<unsigned long>(
                        reinterpret_cast<std::uintptr_t>(&reuse_status)),
                    200);
                if (kResult >= 0) {
                    if (kThird == 4 && reuse_status == 9UL) {
                        char          note[32];
                        unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                        NOTE('h'); NOTE('a'); NOTE('n'); NOTE('d'); NOTE('l');
                        NOTE('e'); NOTE('r'); NOTE(' '); NOTE('i'); NOTE('n');
                        NOTE('h'); NOTE('e'); NOTE('r'); NOTE('i'); NOTE('t');
                        NOTE('e'); NOTE('d'); NOTE(' '); NOTE('o'); NOTE('k');
                        NOTE('\n');
#undef NOTE
                        user_write(note, note_length);
                    }
                    break;
                }
            }
        }
        if (reaped) {
            // Fill every spare slot; the children park until this task ends.
            for (unsigned long index = 0; index < 4; ++index) {
                const long kChild = user_syscall0(cinux::armv7::syscall_nr::kFork);
                if (kChild == 0) {
                    unsigned long parked = 0;
                    for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                        const long kResult = user_syscall3(
                            cinux::armv7::syscall_nr::kWait, 1UL,
                            static_cast<unsigned long>(
                                reinterpret_cast<std::uintptr_t>(&parked)),
                            0);
                        if (kResult >= 0) {
                            break;
                        }
                    }
                    const long kParent = user_syscall3(
                        cinux::armv7::syscall_nr::kGetPpid, 0, 0, 0);
                    if (kParent == 0) {
                        char          note[24];
                        unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                        NOTE('o'); NOTE('r'); NOTE('p'); NOTE('h'); NOTE('a');
                        NOTE('n'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                        user_write(note, note_length);
                    }
                    static_cast<void>(
                        user_syscall3(cinux::armv7::syscall_nr::kExit, 3, 0, 0));
                    for (;;) {
                    }
                }
            }
            const long kOverflow = user_syscall0(cinux::armv7::syscall_nr::kFork);
            if (kOverflow == static_cast<long>(cinux::armv7::errno::kNoMem)) {
                char          note[24];
                unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
                NOTE('n'); NOTE('o'); NOTE(' '); NOTE('s'); NOTE('l'); NOTE('o');
                NOTE('t'); NOTE(' '); NOTE('o'); NOTE('k'); NOTE('\n');
#undef NOTE
                user_write(note, note_length);
            }
        }
        if (reaped && *kSharedPage == 0x1111UL) {
            char          note[32];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('c'); NOTE('o'); NOTE('w'); NOTE(' '); NOTE('a'); NOTE('f');
            NOTE('t'); NOTE('e'); NOTE('r'); NOTE(' '); NOTE('t'); NOTE('w');
            NOTE('o'); NOTE(' '); NOTE('f'); NOTE('o'); NOTE('r'); NOTE('k');
            NOTE('s'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 2) {
        const unsigned long kHandler =
            kTaskACode + (reinterpret_cast<std::uintptr_t>(&user_fault_handler_c) -
                             reinterpret_cast<std::uintptr_t>(__user_text_start));
        static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kSignal, 11, kHandler, 0));
        auto* const kBad = reinterpret_cast<volatile unsigned long*>(0x51000000UL);
        *kBad            = 1;
    }
    if (task_id != 0) {
        // Not mapped in the clone either: this must still end the task.
        static_cast<void>(*reinterpret_cast<volatile unsigned long*>(0x4000B000UL));
    }
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 7, 0, 0));
    for (;;) {
        asm volatile("wfi");
    }
}

bool map_user_page(unsigned long root, unsigned long virtual_address, unsigned long physical) {
    cinux::mm::KernelTables tables;
    cinux::arch::page::Entry* table =
        cinux::mm::walk::EnsureLeafTable(tables, root, virtual_address,
                                         cinux::arch::page::kUser);
    if (table == nullptr) {
        return false;
    }
    table[cinux::arch::page::SlotIndex(virtual_address, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(physical, cinux::arch::page::kUser |
                                                       cinux::arch::page::kWritable);
    return true;
}

/// Gives a cloned task a private stack page and its own argument, so the
/// clone shares code and data but not its stack.
bool privatize_stack(cinux::mm::AddressSpace& space, const cinux::mm::AddressSpace& parent,
                     unsigned long stack_va, unsigned long index) {
    const unsigned long kParentFrame = parent.translate(stack_va);
    if (kParentFrame == 0) {
        return false;
    }
    const auto kFrame = cinux::mm::Pmm::self().allocate_page();
    if (kFrame.raw == 0) {
        return false;
    }
    const auto* const source = reinterpret_cast<const unsigned char*>(kParentFrame);
    auto* const       target = reinterpret_cast<unsigned char*>(kFrame.raw);
    for (unsigned long offset = 0; offset < cinux::arch::page::kSize; ++offset) {
        target[offset] = source[offset];
    }
    *reinterpret_cast<unsigned long*>(kFrame.raw + cinux::arch::page::kSize - 4) = index;
    if (!space.map_user(stack_va, kFrame.raw)) {
        return false;
    }
    cinux::mm::cow::ReleaseFrame(kParentFrame);  // the clone stops sharing the parent stack
    // With the clone off it, the parent's stack is exclusive again, so
    // put its writable permission back; a read-only exclusive page would
    // fault with nobody to answer.
    return const_cast<cinux::mm::AddressSpace&>(parent).map_user(stack_va, kParentFrame);
}

bool setup_task(unsigned long index, unsigned long code_va, unsigned long stack_va,
                cinux::mm::AddressSpace* parent = nullptr) {
    UserTask& task = g_tasks[index];
    task            = UserTask{};
    task.state       = TaskState::kRunnable;
    task.exit_status = 0;
    task.wait_target   = kTaskCount;
    task.wait_mask     = 0;
    task.wait_deadline = 0;
    task.break_top     = kBrkBase;
    task.fds[0] = FdEntry{true, FdKind::kConsoleIn, 0};
    task.fds[1] = FdEntry{true, FdKind::kConsoleOut, 0};
    task.fds[2] = FdEntry{true, FdKind::kConsoleOut, 0};
    task.fault_handler = 0;
    task.in_use        = true;
    task.parent        = 0;
    task.mmap_top      = kMmapBase;
    task.read_pending  = false;
    task.read_buf      = 0;
    task.read_count    = 0;
    task.space     = new cinux::mm::AddressSpace{};
    if (task.space == nullptr || !task.space->init()) {
        return false;
    }
    if (parent != nullptr) {
        if (!cinux::armv7::CloneUserSpace(*parent, *task.space) ||
            !privatize_stack(*task.space, *parent, stack_va, index)) {
            return false;
        }
        task.trap      = cinux::arch::UserTrap{};
        task.trap.lr   = code_va + static_cast<unsigned long>(
                                       reinterpret_cast<std::uintptr_t>(&user_entry_stub) -
                                       reinterpret_cast<std::uintptr_t>(__user_text_start));
        task.trap.sp   = stack_va + cinux::arch::page::kSize;
        task.trap.spsr = 0x10UL;
        return true;
    }
    const auto kStackFrame = cinux::mm::Pmm::self().allocate_page();
    if (kStackFrame.raw == 0) {
        return false;
    }
    // The user section outgrew one page once the self-check grew; copy it
    // into as many frames as it needs and map them back to back.
    const unsigned long kTextBytes =
        static_cast<unsigned long>(__user_text_end - __user_text_start);
    const unsigned long kCodePages =
        (kTextBytes + cinux::arch::page::kSize - 1) / cinux::arch::page::kSize;
    if (code_va + kCodePages * cinux::arch::page::kSize > stack_va) {
        cinux::print::Println("[armv7] user text needs %u pages, only %u reserved",
                              kCodePages,
                              (stack_va - code_va) / cinux::arch::page::kSize);
        return false;
    }
    for (unsigned long index = 0; index < kCodePages; ++index) {
        const auto kCodeFrame = cinux::mm::Pmm::self().allocate_page();
        if (kCodeFrame.raw == 0) {
            return false;
        }
        auto* const code = reinterpret_cast<unsigned char*>(kCodeFrame.raw);
        for (unsigned long offset = 0; offset < cinux::arch::page::kSize; ++offset) {
            const unsigned long source = index * cinux::arch::page::kSize + offset;
            code[offset] = source < kTextBytes ? __user_text_start[source] : 0U;
        }
        if (!task.space->map_user(code_va + index * cinux::arch::page::kSize,
                                  kCodeFrame.raw)) {
            return false;
        }
    }
    if (!task.space->map_user(stack_va, kStackFrame.raw)) {
        return false;
    }
    if (g_shared_frame != 0 &&
        !cinux::armv7::MapSharedFrame(*task.space, 0x40008000UL, static_cast<unsigned long>(g_shared_frame))) {
        return false;
    }
    // The task's argument waits at the top of its own stack; the copied
    // stub reads it before any C prologue can move sp.
    *reinterpret_cast<unsigned long*>(kStackFrame.raw + cinux::arch::page::kSize - 4) = index;
    const unsigned long kEntry =
        code_va + static_cast<unsigned long>(
                      reinterpret_cast<std::uintptr_t>(&user_entry_stub) -
                      reinterpret_cast<std::uintptr_t>(__user_text_start));
    task.trap      = cinux::arch::UserTrap{};
    task.trap.lr   = kEntry;
    task.trap.sp   = stack_va + cinux::arch::page::kSize;
    task.trap.spsr = 0x10UL;  // user mode
    return true;
}

}  // namespace

extern "C" {
cinux::arch::UserTrap g_user_trap;
}

namespace cinux::armv7 {

bool RepairUserPage(unsigned long address, unsigned long status) {
    // A write to a shared, read-only page gets a private copy: the frame
    // the leaf names is duplicated, this space points at the copy, and
    // this owner leaves the shared ledger.
    const unsigned long kPage = address & ~(cinux::arch::page::kSize - 1UL);
    {
        cinux::mm::KernelTables tables;
        const unsigned long     root = cinux::arch::space::ActiveRoot();
        const unsigned long     frame = cinux::mm::walk::TranslatePage(tables, root, kPage);
        if (frame != 0 && (status & cinux::arch::isr::kDfsrWrite) != 0 &&
            cinux::mm::cow::FrameShares(frame) >= 2) {
            const auto kCopy = cinux::mm::Pmm::self().allocate_page();
            if (kCopy.raw == 0) {
                return false;
            }
            const auto* const source = reinterpret_cast<const unsigned char*>(frame);
            auto* const       target = reinterpret_cast<unsigned char*>(kCopy.raw);
            for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
                target[index] = source[index];
            }
            auto* const table = cinux::mm::walk::EnsureLeafTable(tables, root, kPage,
                                                                 cinux::arch::page::kUser);
            if (table == nullptr) {
                return false;
            }
            table[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
                cinux::arch::page::MakePageEntry(
                    kCopy.raw, cinux::arch::page::kUser | cinux::arch::page::kWritable);
            cinux::arch::space::InvalidatePage(kPage);
            cinux::mm::cow::ReleaseFrame(frame);
            cinux::print::Println("[armv7] cow copy at %X", kPage);
            return true;
        }
    }

    cinux::mm::KernelTables tables;
    const unsigned long     root = cinux::arch::space::ActiveRoot();
    const unsigned long     mapped = cinux::mm::walk::TranslatePage(tables, root, kPage);
    if (mapped != 0 && (status & cinux::arch::isr::kDfsrWrite) != 0 &&
        !cinux::arch::UserPageIsWritable(kPage)) {
        // A private page mapped read-only: copy it and hand back a
        // writable frame. Nobody else shares it, so the old one goes.
        const auto kCopy = cinux::mm::Pmm::self().allocate_page();
        if (kCopy.raw == 0) {
            return false;
        }
        const auto* const source = reinterpret_cast<const unsigned char*>(mapped);
        auto* const       target = reinterpret_cast<unsigned char*>(kCopy.raw);
        for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
            target[index] = source[index];
        }
        auto* const table = cinux::mm::walk::EnsureLeafTable(tables, root, kPage,
                                                             cinux::arch::page::kUser);
        if (table == nullptr) {
            return false;
        }
        table[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
            cinux::arch::page::MakePageEntry(
                kCopy.raw, cinux::arch::page::kUser | cinux::arch::page::kWritable);
        cinux::arch::space::InvalidatePage(kPage);
        cinux::mm::cow::ReleaseFrame(mapped);
        cinux::print::Println("[armv7] ro copy at %X", kPage);
        return true;
    }

    // Only the demand-page window is fillable; every other fault (the
    // cross-table probe included) must fall through to the task ending.
    constexpr unsigned long kDemandBase = 0x40009000UL;
    constexpr unsigned long kDemandEnd  = 0x4000A000UL;
    if (address < kDemandBase || address >= kDemandEnd) {
        return false;
    }
    if (mapped != 0) {
        return false;
    }
    const auto kFrame = cinux::mm::Pmm::self().allocate_page();
    if (kFrame.raw == 0) {
        return false;
    }
    auto* const table = cinux::mm::walk::EnsureLeafTable(tables, root, kPage,
                                                         cinux::arch::page::kUser);
    if (table == nullptr) {
        return false;
    }
    table[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(kFrame.raw,
                                         cinux::arch::page::kUser | cinux::arch::page::kWritable);
    cinux::arch::space::InvalidatePage(kPage);
    cinux::print::Println("[armv7] fault repaired at %X", kPage);
    return true;
}

__attribute__((noinline)) bool RunUserModeSelfCheck() {
    InstallFaultRepair(&RepairUserPage);
    const auto kShared = cinux::mm::Pmm::self().allocate_page();
    if (kShared.raw == 0) {
        return false;
    }
    g_shared_frame = kShared.raw;
    g_kernel_root = cinux::arch::space::ActiveRoot();
    cinux::print::Println("[armv7] user text %u bytes in %u pages",
                          static_cast<unsigned>(__user_text_end - __user_text_start),
                          static_cast<unsigned>(
                              (static_cast<unsigned long>(__user_text_end - __user_text_start) +
                               cinux::arch::page::kSize - 1) /
                              cinux::arch::page::kSize));
    if (!setup_task(0, kTaskACode, kTaskAStack)) {
        return false;
    }
    if (!setup_task(1, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    if (!setup_task(2, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    if (!setup_task(3, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    for (unsigned long slot = kInitialTasks; slot < kTaskCount; ++slot) {
        g_tasks[slot]           = UserTask{};
        g_tasks[slot].state     = TaskState::kDone;
        g_tasks[slot].in_use    = false;
        g_tasks[slot].wait_target = kTaskCount;
    }
    g_running = true;
    g_current = 0;
    cinux::arch::per_cpu::self().slice_ticks = 1;
    cinux::print::Println("[armv7] task 0 starts on its own table");
    cinux::arch::space::SwitchRoot(g_tasks[0].space->root());
    cinux::arch::EnterUserModeWithReturn(g_tasks[0].trap.lr, g_tasks[0].trap.sp, &g_main);
after_drop:
    cinux::mm::cow::ReleaseFrame(g_shared_frame);
    cinux::arch::space::SwitchRoot(g_kernel_root);
    bool all_done = true;
    for (const auto& task : g_tasks) {
        all_done = all_done && task.state == TaskState::kDone;
    }
    return all_done;
}

}  // namespace cinux::armv7

namespace {

/// Picks the next runnable task and hands its frame back to the stub.
/// Never returns when nothing is runnable: it resumes the kernel instead.
/// Wakes the tasks blocked on `target`; each one re-asks on return.
void wake_waiters_of(unsigned long target) {
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked && ((task.wait_mask >> target) & 1UL) != 0) {
            task.state         = TaskState::kRunnable;
            task.wait_target   = kTaskCount;
            task.wait_mask     = 0;
            task.wait_deadline = 0;
            cinux::print::Println("[armv7] wake a waiter of %u", target);
        }
    }
}

/// Copies a line (or up to `count` bytes) from the console into a user
/// buffer. Returns the byte count, or -1 when the pointer is refused.
long DrainConsole(unsigned long buffer, unsigned long count) {
    unsigned long received = 0;
    while (received < count && cinux::armv7::UartReceiveReady()) {
        const char          kChar = cinux::armv7::UartReadChar();
        const unsigned char kByte = static_cast<unsigned char>(kChar);
        if (!cinux::arch::CopyToUser(buffer + received, &kByte, 1)) {
            return -1;
        }
        ++received;
        if (kChar == '\n' || kChar == '\r') {
            break;
        }
    }
    return static_cast<long>(received);
}

/// Returns one task's space to the allocator. Called only after the
/// scheduler has switched away from it, so its tables are not active.
void teardown_space(unsigned long index) {
    if (g_tasks[index].space == nullptr) {
        return;
    }
    g_tasks[index].space->teardown();
    delete g_tasks[index].space;
    g_tasks[index].space  = nullptr;
    g_tasks[index].in_use = false;  // the slot is free for a later fork
    for (unsigned long child = 0; child < kTaskCount; ++child) {
        if (g_tasks[child].parent == index) {
            // Its parent is gone: reparent to the boot task, or to nobody
            // when the boot task itself is the one leaving.
            g_tasks[child].parent = index == 0 ? kNoParent : 0;
        }
    }
}

/// The next runnable task after `from`, or kTaskCount when there is none.
unsigned long next_runnable(unsigned long from) {
    for (unsigned long step = 1; step < kTaskCount; ++step) {
        const unsigned long candidate = (from + step) % kTaskCount;
        if (g_tasks[candidate].state == TaskState::kRunnable) {
            return candidate;
        }
    }
    return kTaskCount;
}

cinux::arch::UserTrap* schedule_next(unsigned long number) {
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&g_user_trap.r0)[word];
    }
    if (number == cinux::armv7::syscall_nr::kExit) {
        g_tasks[g_current].state       = TaskState::kDone;
        g_tasks[g_current].exit_status = g_user_trap.r0;
        cinux::print::Println("[armv7] task %u exited with status %u", g_current,
                              g_user_trap.r0);
        wake_waiters_of(g_current);
    }

    if (number == cinux::armv7::syscall_nr::kLseek) {
        const unsigned long kFd     = g_user_trap.r0;
        const long          kOffset = static_cast<long>(g_user_trap.r1);
        const unsigned long kWhence = g_user_trap.r2;
        if (kFd >= kMaxFds || !g_tasks[g_current].fds[kFd].open ||
            g_tasks[g_current].fds[kFd].kind != FdKind::kFile) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kBadFd);
            return &g_tasks[g_current].trap;
        }
        FdEntry&      file = g_tasks[g_current].fds[kFd];
        const long    base = kWhence == 0 ? 0L
                             : kWhence == 1 ? static_cast<long>(file.offset)
                                            : static_cast<long>(file.size);
        const long    next = base + kOffset;
        if (next < 0 || kWhence > 2) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kInvalid);
            cinux::print::Println("[armv7] lseek %u refused, whence %u", kFd, kWhence);
            return &g_tasks[g_current].trap;
        }
        file.offset = static_cast<unsigned long>(next);
        cinux::print::Println("[armv7] lseek %u -> %u", kFd, file.offset);
        g_tasks[g_current].trap.r0 = file.offset;
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kOpen) {
        // One byte at a time: a stack string may end just before the page
        // boundary, and a single wide copy would be refused wholesale.
        char          path[16] = {};
        unsigned long length   = 0;
        bool          copied   = true;
        while (length + 1 < sizeof(path)) {
            if (!cinux::arch::CopyFromUser(&path[length], g_user_trap.r0 + length, 1)) {
                copied = false;
                break;
            }
            if (path[length] == '\0') {
                break;
            }
            ++length;
        }
        if (!copied) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kFault);
            return &g_tasks[g_current].trap;
        }
        const BuiltinFile* found = nullptr;
        for (const auto& entry : kBuiltinFiles) {
            if (length != entry.name_length) {
                continue;
            }
            bool same = true;
            for (unsigned long index = 0; index < length; ++index) {
                if (path[index] != entry.name[index]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                found = &entry;
                break;
            }
        }
        const bool kScratch = length == 8 && path[0] == '/' && path[1] == 's' &&
                              path[2] == 'c' && path[3] == 'r' && path[4] == 'a' &&
                              path[5] == 't' && path[6] == 'c' && path[7] == 'h';
        if (found == nullptr && !kScratch) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kNoEnt);
            cinux::print::Println("[armv7] open refused");
            return &g_tasks[g_current].trap;
        }
        unsigned long slot = kMaxFds;
        for (unsigned long index = 3; index < kMaxFds; ++index) {
            if (!g_tasks[g_current].fds[index].open) {
                slot = index;
                break;
            }
        }
        if (slot == kMaxFds) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
            cinux::print::Println("[armv7] open refused, no free slot");
            return &g_tasks[g_current].trap;
        }
        g_tasks[g_current].fds[slot].open     = true;
        g_tasks[g_current].fds[slot].kind     = FdKind::kFile;
        g_tasks[g_current].fds[slot].offset   = 0;
        g_tasks[g_current].fds[slot].data     = found != nullptr ? found->data : g_scratch_file;
        g_tasks[g_current].fds[slot].size     = found != nullptr ? found->size : g_scratch_len;
        g_tasks[g_current].fds[slot].writable = found == nullptr;
        cinux::print::Println("[armv7] open %s as %u",
                              found != nullptr ? found->name + 1 : "scratch", slot);
        g_tasks[g_current].trap.r0 = slot;
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kRead) {
        const unsigned long kFd    = g_user_trap.r0;
        const unsigned long kBuf   = g_user_trap.r1;
        const unsigned long kCount = g_user_trap.r2;
        if (kFd >= kMaxFds || !g_tasks[g_current].fds[kFd].open) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kBadFd);
            return &g_tasks[g_current].trap;
        }
        if (g_tasks[g_current].fds[kFd].kind == FdKind::kFile) {
            FdEntry&            file = g_tasks[g_current].fds[kFd];
            const unsigned long left = file.size - file.offset;
            const unsigned long take = kCount < left ? kCount : left;
            if (take == 0 ||
                !cinux::arch::CopyToUser(kBuf, file.data + file.offset, take)) {
                g_tasks[g_current].trap.r0 = 0;
                return &g_tasks[g_current].trap;
            }
            file.offset += take;
            cinux::print::Println("[armv7] file read %u", take);
            g_tasks[g_current].trap.r0 = take;
            return &g_tasks[g_current].trap;
        }
        if (g_tasks[g_current].fds[kFd].kind != FdKind::kConsoleIn) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kBadFd);
            return &g_tasks[g_current].trap;
        }
        if (kCount == 0 || !cinux::arch::UserRangeIsMapped(kBuf, kCount)) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kFault);
            cinux::print::Println("[armv7] read refused %X + %u", kBuf, kCount);
            return &g_tasks[g_current].trap;
        }
        if (cinux::armv7::UartReceiveReady()) {
            const long kBytes = DrainConsole(kBuf, kCount);
            cinux::print::Println("[armv7] read %d bytes", static_cast<int>(kBytes));
            g_tasks[g_current].trap.r0 = static_cast<unsigned long>(kBytes);
            return &g_tasks[g_current].trap;
        }
        // Nothing yet: park until input arrives or the deadline passes.
        const unsigned long kTimeout = g_user_trap.r3 == 0 ? kReadTicks : g_user_trap.r3;
        g_tasks[g_current].read_pending  = true;
        g_tasks[g_current].read_buf      = kBuf;
        g_tasks[g_current].read_count    = kCount;
        g_tasks[g_current].state         = TaskState::kBlocked;
        g_tasks[g_current].wait_mask     = 0;
        g_tasks[g_current].wait_deadline = g_tick_now + kTimeout;
        g_tasks[g_current].trap.r0       = 0;
        cinux::print::Println("[armv7] read waits for input");
    }

    if (number == cinux::armv7::syscall_nr::kFork) {
        unsigned long slot = kTaskCount;
        for (unsigned long index = kInitialTasks; index < kTaskCount; ++index) {
            if (!g_tasks[index].in_use) {
                slot = index;
                break;
            }
        }
        if (slot == kTaskCount) {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
            cinux::print::Println("[armv7] fork refused, no free slot");
            return &g_tasks[g_current].trap;
        }
        UserTask& parent = g_tasks[g_current];
        auto* const space = new cinux::mm::AddressSpace{};
        if (space == nullptr || !space->init() ||
            !cinux::armv7::CloneUserSpace(*parent.space, *space) ||
            !privatize_stack(*space, *parent.space, kTaskAStack, slot)) {
            delete space;
            parent.trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
            cinux::print::Println("[armv7] fork refused, clone failed");
            return &parent.trap;
        }
        UserTask& child     = g_tasks[slot];
        child               = UserTask{};
        child.in_use        = true;
        child.state         = TaskState::kRunnable;
        child.space         = space;
        child.break_top     = parent.break_top;
        child.mmap_top      = parent.mmap_top;
        for (unsigned long fd = 0; fd < kMaxFds; ++fd) {
            child.fds[fd] = parent.fds[fd];
        }
        child.fault_handler = parent.fault_handler;
        child.parent        = g_current;
        child.wait_target   = kTaskCount;
        child.trap          = parent.trap;
        child.trap.r0       = 0;  // the child's fork returns zero
        cinux::print::Println("[armv7] fork %u -> %u", g_current, slot);
        parent.trap.r0 = slot;
        return &parent.trap;
    }

    if (number == cinux::armv7::syscall_nr::kSignal) {
        // Signal is spelled narrow here: only SIGSEGV, only a handler.
        const unsigned long kSigno   = g_user_trap.r0;
        const unsigned long kHandler = g_user_trap.r1;
        if (kSigno == 11) {
            g_tasks[g_current].fault_handler = kHandler;
            cinux::print::Println("[armv7] fault handler %X for task %u", kHandler, g_current);
            g_tasks[g_current].trap.r0 = 0;
        } else {
            g_tasks[g_current].trap.r0 =
                static_cast<unsigned long>(cinux::armv7::errno::kInvalid);
        }
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kMmap) {
        // Anonymous mapping. r0 is the byte length, r1 an optional address
        // hint (0 lets the bump window choose). A hint must sit inside the
        // window, be page-aligned, and name unmapped pages.
        const unsigned long kBytes = g_user_trap.r0;
        const unsigned long kHint  = g_user_trap.r1;
        const unsigned long kFlags = g_user_trap.r2;
        unsigned long&      kTop   = g_tasks[g_current].mmap_top;
        if (kFlags > 1UL) {
            g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kInvalid);
            cinux::print::Println("[armv7] mmap flags %X refused", kFlags);
            return &g_tasks[g_current].trap;
        }
        const unsigned long kWant  = (kBytes + 4095UL) & ~4095UL;
        unsigned long       kBase  = 0;
        if (kHint != 0) {
            if (kHint < kMmapBase || kHint >= kMmapLimit || (kHint & 4095UL) != 0) {
                g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kInvalid);
                cinux::print::Println("[armv7] mmap hint %X refused", kHint);
                return &g_tasks[g_current].trap;
            }
            cinux::mm::KernelTables tables;
            const unsigned long     root = g_tasks[g_current].space->root();
            for (unsigned long spot = kHint; spot < kHint + kWant;
                 spot += cinux::arch::page::kSize) {
                if (cinux::mm::walk::TranslatePage(tables, root, spot) != 0) {
                    g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
                    cinux::print::Println("[armv7] mmap hint %X is taken", kHint);
                    return &g_tasks[g_current].trap;
                }
            }
            kBase = kHint;
        } else if (kTop + kWant <= kMmapLimit) {
            kBase = kTop;
            kTop += kWant;
        } else {
            g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
            cinux::print::Println("[armv7] mmap refused %u", kBytes);
            return &g_tasks[g_current].trap;
        }
        cinux::mm::KernelTables tables;
        const unsigned long     root = g_tasks[g_current].space->root();
        for (unsigned long spot = kBase; spot < kBase + kWant;
             spot += cinux::arch::page::kSize) {
            const auto kFrame = cinux::mm::Pmm::self().allocate_page();
            if (kFrame.raw == 0) {
                g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
                return &g_tasks[g_current].trap;
            }
            auto* const table = cinux::mm::walk::EnsureLeafTable(
                tables, root, spot, cinux::arch::page::kUser);
            if (table == nullptr) {
                cinux::mm::Pmm::self().free_page(kFrame);
                g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kNoMem);
                return &g_tasks[g_current].trap;
            }
            const auto kLeafBits =
                kFlags != 0
                    ? cinux::arch::page::kUser | cinux::arch::page::kReadOnly
                    : cinux::arch::page::kUser | cinux::arch::page::kWritable;
            table[cinux::arch::page::SlotIndex(spot, cinux::arch::page::kLeafLevel)] =
                cinux::arch::page::MakePageEntry(kFrame.raw, kLeafBits);
            cinux::arch::space::InvalidatePage(spot);
        }
        cinux::print::Println("[armv7] mmap %X len %X flags %X", kBase, kWant, kFlags);
        g_tasks[g_current].trap.r0 = kBase;
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kMunmap) {
        // Only the anonymous window: the stack and the code are not
        // this call's to release.
        const unsigned long kAddr  = g_user_trap.r0;
        const unsigned long kBytes = g_user_trap.r1;
        if (kAddr < kMmapBase || (kAddr & 4095UL) != 0 || kBytes == 0) {
            g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kInvalid);
            return &g_tasks[g_current].trap;
        }
        const unsigned long kEnd = (kAddr + kBytes + 4095UL) & ~4095UL;
        cinux::mm::KernelTables tables;
        const unsigned long     root = g_tasks[g_current].space->root();
        auto* const             l1   = reinterpret_cast<cinux::arch::page::Entry*>(root);
        unsigned long           freed = 0;
        for (unsigned long spot = kAddr; spot < kEnd; spot += cinux::arch::page::kSize) {
            const auto kL1 = l1[(spot >> 20U) & 0xFFFUL];
            if (!cinux::arch::page::DescriptorPresent(kL1) ||
                cinux::arch::page::DescriptorIsLeaf(kL1, cinux::arch::page::WalkLevel::kL1)) {
                continue;
            }
            auto* const l2 = reinterpret_cast<cinux::arch::page::Entry*>(
                cinux::arch::page::TableTarget(kL1));
            auto& slot = l2[(spot >> 12U) & 0xFFUL];
            if (!cinux::arch::page::DescriptorPresent(slot)) {
                continue;
            }
            cinux::mm::cow::ReleaseFrame(cinux::arch::page::LeafFrame(
                slot, cinux::arch::page::kLeafLevel));
            slot = cinux::arch::page::Entry{0};
            cinux::arch::space::InvalidatePage(spot);
            ++freed;
        }
        cinux::print::Println("[armv7] munmap %X pages %u", kAddr, freed);
        g_tasks[g_current].trap.r0 = 0;
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kClose) {
        const unsigned long kFd = g_user_trap.r0;
        if (kFd <= 2) {
            g_tasks[g_current].fds[kFd].open = false;
            cinux::print::Println("[armv7] close %u", kFd);
            g_tasks[g_current].trap.r0 = 0;
        } else {
            g_tasks[g_current].trap.r0 = static_cast<unsigned long>(cinux::armv7::errno::kBadFd);
        }
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kBrk) {
        // Grow-only: round up to a page, map what is new, refuse past the
        // window or below the base by reporting the current break.
        const unsigned long kAsked = g_user_trap.r0;
        unsigned long&      kBreak = g_tasks[g_current].break_top;
        if (kAsked >= kBrkBase && kAsked <= kBrkLimit) {
            const unsigned long kTop = (kAsked + 4095UL) & ~4095UL;
            cinux::mm::KernelTables tables;
            const unsigned long     root = g_tasks[g_current].space->root();
            while (kBreak < kTop) {
                const auto kFrame = cinux::mm::Pmm::self().allocate_page();
                if (kFrame.raw == 0) {
                    break;
                }
                auto* const table = cinux::mm::walk::EnsureLeafTable(
                    tables, root, kBreak, cinux::arch::page::kUser);
                if (table == nullptr) {
                    cinux::mm::Pmm::self().free_page(kFrame);
                    break;
                }
                table[cinux::arch::page::SlotIndex(kBreak, cinux::arch::page::kLeafLevel)] =
                    cinux::arch::page::MakePageEntry(
                        kFrame.raw, cinux::arch::page::kUser | cinux::arch::page::kWritable);
                cinux::arch::space::InvalidatePage(kBreak);
                kBreak += cinux::arch::page::kSize;
            }
        }
        g_tasks[g_current].trap.r0 = kBreak;
        cinux::print::Println("[armv7] brk -> %X", kBreak);
        return &g_tasks[g_current].trap;
    }

    if (number == cinux::armv7::syscall_nr::kSleep) {
        const unsigned long kTicks = g_user_trap.r0;
        if (kTicks == 0) {
            return &g_tasks[g_current].trap;
        }
        g_tasks[g_current].state         = TaskState::kBlocked;
        g_tasks[g_current].wait_mask     = 0;
        g_tasks[g_current].wait_target   = kTaskCount;
        g_tasks[g_current].wait_deadline = g_tick_now + kTicks;
        g_tasks[g_current].trap.r0       = 0;
        cinux::print::Println("[armv7] task %u sleeps %u ticks", g_current, kTicks);
    }

    if (number == cinux::armv7::syscall_nr::kWait) {
        const unsigned long kMask = g_user_trap.r0;
        unsigned long       kDone = kTaskCount;
        for (unsigned long index = 0; index < kTaskCount; ++index) {
            if (((kMask >> index) & 1UL) != 0 && g_tasks[index].state == TaskState::kDone) {
                kDone = index;
                break;
            }
        }
        if (kDone != kTaskCount) {
            const unsigned long kStatus = g_tasks[kDone].exit_status;
            if (cinux::arch::CopyToUser(g_user_trap.r1, &kStatus, sizeof(kStatus))) {
                cinux::print::Println("[armv7] wait %u -> %X", kDone, kStatus);
                g_tasks[g_current].trap.r0 = kDone;
            } else {
                g_tasks[g_current].trap.r0 = ~0UL;
            }
            return &g_tasks[g_current].trap;
        }
        // Not finished: park this task; when someone ends, wake_blocked
        // makes it runnable again and it re-asks with -1 in hand.
        const unsigned long kTimeout =
            g_user_trap.r2 == 0 ? kWaitTicks : g_user_trap.r2;
        g_tasks[g_current].state         = TaskState::kBlocked;
        g_tasks[g_current].wait_mask     = kMask;
        g_tasks[g_current].wait_deadline = g_tick_now + kTimeout;
        g_tasks[g_current].trap.r0       = ~0UL;
        cinux::print::Println("[armv7] task %u blocks on %X", g_current, kMask);
    }

    const unsigned long kOld  = g_current;
    const unsigned long kNext = next_runnable(g_current);
    if (kNext != kTaskCount) {
        g_current = kNext;
        cinux::print::Println("[armv7] switch to task %u", kNext);
        cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
        if (g_tasks[kOld].state == TaskState::kDone) {
            teardown_space(kOld);
        }
        cinux::arch::per_cpu::self().slice_ticks = 1;
        return &g_tasks[kNext].trap;
    }

    if (g_tasks[g_current].state == TaskState::kRunnable) {
        // The others finished; this task keeps running.
        return &g_tasks[g_current].trap;
    }

    // Nothing runnable but someone still waits: wake them as expired so
    // they can observe the timeout instead of hanging forever.
    bool woke = false;
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked) {
            const unsigned long kIndex = &task - &g_tasks[0];
            task.state         = TaskState::kRunnable;
            if (task.read_pending) {
                task.read_pending = false;
                task.trap.r0      = 0;
                cinux::print::Println("[armv7] task %u read timed out", kIndex);
            } else {
                task.trap.r0 = task.wait_mask == 0 ? 0 : static_cast<unsigned long>(-2);
            }
            task.wait_mask     = 0;
            task.wait_deadline = 0;
            woke               = true;
        }
    }
    if (woke) {
        const unsigned long kRetry = next_runnable(g_current);
        if (kRetry != kTaskCount) {
            g_current = kRetry;
            cinux::print::Println("[armv7] switch to task %u", kRetry);
            cinux::arch::space::SwitchRoot(g_tasks[kRetry].space->root());
            cinux::arch::per_cpu::self().slice_ticks = 1;
            return &g_tasks[kRetry].trap;
        }
    }

    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    teardown_space(kOld);
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}

}  // namespace

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number) {
    return schedule_next(number);
}

extern "C" void SchedulerOnTick(unsigned long ticks) {
    g_tick_now = ticks;
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked && task.read_pending &&
            cinux::armv7::UartReceiveReady()) {
            const long kBytes = DrainConsole(task.read_buf, task.read_count);
            task.read_pending = false;
            task.state        = TaskState::kRunnable;
            task.trap.r0      = static_cast<unsigned long>(kBytes);
            cinux::print::Println("[armv7] read %d bytes after waiting", static_cast<int>(kBytes));
            continue;
        }
        if (task.state == TaskState::kBlocked && task.wait_deadline <= ticks) {
            task.state         = TaskState::kRunnable;
            task.wait_target   = kTaskCount;
            const unsigned long kIndex = &task - &g_tasks[0];
            if (task.wait_mask == 0 && task.read_pending) {
                task.read_pending = false;
                task.trap.r0      = 0;
                cinux::print::Println("[armv7] task %u read timed out", kIndex);
            } else if (task.wait_mask == 0) {
                task.trap.r0 = 0;
                cinux::print::Println("[armv7] task %u woke from sleep", kIndex);
            } else {
                task.trap.r0 = static_cast<unsigned long>(-2);
                cinux::print::Println("[armv7] task %u wait timed out", kIndex);
            }
            task.wait_mask = 0;
        }
    }
}

extern "C" bool CurrentFdIsFile(unsigned long fd) {
    return fd < kMaxFds && g_tasks[g_current].fds[fd].open &&
           g_tasks[g_current].fds[fd].kind == FdKind::kFile;
}

extern "C" long FileWrite(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (!CurrentFdIsFile(fd) || !g_tasks[g_current].fds[fd].writable) {
        cinux::print::Println("[armv7] write %u refused, read-only", fd);
        return cinux::armv7::errno::kBadFd;
    }
    FdEntry&            file = g_tasks[g_current].fds[fd];
    const unsigned long room = sizeof(g_scratch_file) - file.offset;
    if (room == 0 || count > room) {
        return cinux::armv7::errno::kNoSpc;
    }
    if (!cinux::arch::CopyFromUser(g_scratch_file + file.offset, buffer, count)) {
        return cinux::armv7::errno::kFault;
    }
    file.offset += count;
    if (file.offset > g_scratch_len) {
        g_scratch_len = file.offset;
    }
    file.size          = g_scratch_len;
    cinux::print::Println("[armv7] file write %u", count);
    return static_cast<long>(count);
}

extern "C" bool CurrentFdReadable(unsigned long fd) {
    if (fd >= kMaxFds || !g_tasks[g_current].fds[fd].open) {
        return false;
    }
    return g_tasks[g_current].fds[fd].kind == FdKind::kConsoleIn;
}

extern "C" bool CurrentFdWritable(unsigned long fd) {
    if (fd >= kMaxFds || !g_tasks[g_current].fds[fd].open) {
        return false;
    }
    return g_tasks[g_current].fds[fd].kind == FdKind::kConsoleOut;
}

extern "C" unsigned long CurrentTaskIndex() {
    return g_current;
}

extern "C" unsigned long CurrentTick() {
    return g_tick_now;
}

extern "C" unsigned long CurrentTaskParent() {
    const unsigned long kParent = g_tasks[g_current].parent;
    return kParent == kNoParent ? 0UL : kParent;
}

extern "C" bool SchedulerRunning() {
    return g_running;
}

extern "C" bool TaskIsDone(unsigned long index) {
    return index < kTaskCount && g_tasks[index].state == TaskState::kDone;
}

extern "C" unsigned long TaskExitStatus(unsigned long index) {
    if (index >= kTaskCount || g_tasks[index].state != TaskState::kDone) {
        return 0;
    }
    return g_tasks[index].exit_status;
}

extern "C" cinux::arch::UserTrap* c_preempt_from_irq(cinux::arch::UserTrap* trap) {
    if (!g_running) {
        return trap;
    }
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&trap->r0)[word];
    }
    const unsigned long kNext = next_runnable(g_current);
    if (kNext == kTaskCount) {
        return trap;
    }
    g_current = kNext;
    cinux::print::Println("[armv7] preempt to task %u", kNext);
    cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
    cinux::arch::per_cpu::self().slice_ticks = 1;
    return &g_tasks[kNext].trap;
}

extern "C" void ResumeTrap(cinux::arch::UserTrap* frame);

/// A fault with a registered handler resumes in user mode with the handler
/// in place of the faulting instruction; without one it ends the task.
extern "C" cinux::arch::UserTrap* c_user_fault_handler(cinux::arch::UserTrap* trap,
                                                       unsigned long address,
                                                       unsigned long code) {
    if (g_tasks[g_current].fault_handler == 0) {
        return nullptr;
    }
    cinux::print::Println("[armv7] fault handled at %X by %X", address,
                          g_tasks[g_current].fault_handler);
    trap->r0   = address;
    trap->r1   = code;
    trap->lr   = g_tasks[g_current].fault_handler;
    trap->spsr = 0x10UL;  // user mode, interrupts on
    return trap;
}

/// A user task died on an abort: mark it, run the other one, or go home.
extern "C" [[noreturn]] void c_user_faulted() {
    g_tasks[g_current].state       = TaskState::kDone;
    g_tasks[g_current].exit_status = 0xDEADUL;
    cinux::print::Println("[armv7] task %u faulted", g_current);
    wake_waiters_of(g_current);
    const unsigned long kOld  = g_current;
    const unsigned long kNext = next_runnable(g_current);
    if (kNext != kTaskCount) {
        g_current = kNext;
        cinux::print::Println("[armv7] switch to task %u", kNext);
        cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
        teardown_space(kOld);
        asm volatile("cps #0x13" ::: "memory");
        ResumeTrap(&g_tasks[kNext].trap);
    }
    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    teardown_space(kOld);
    asm volatile("cps #0x13" ::: "memory");
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}
