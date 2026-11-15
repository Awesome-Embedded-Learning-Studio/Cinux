/**
 * @file    armv7_diskprog.c
 * @brief   The ext2-resident program the guest execs into.
 *
 * Baked into the SD card's first partition; the seated world replaces
 * itself with this image through the neutral execve face, proving a
 * program can arrive from the real disk street, not only the boot
 * blob. It writes one line and exits zero.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1
 * @since   0.1.0
 * @ingroup assets
 * @copyright Copyright (c) 2026
 */

static long sys3(unsigned long number, unsigned long a, unsigned long b,
                 unsigned long c) {
    register unsigned long r7 __asm("r7") = number;
    register unsigned long r0 __asm("r0") = a;
    register unsigned long r1 __asm("r1") = b;
    register unsigned long r2 __asm("r2") = c;
    __asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2)
                     : "memory");
    return (long)r0;
}

static unsigned long slen(const char* text) {
    unsigned long n = 0;
    while (text[n] != 0) {
        ++n;
    }
    return n;
}

void disk_start(void) {
    static const char kLine[] = "[disk] hello from ext2, exec replaced me\n";
    static const char kNext[] = "/glibc";
    sys3(4UL, 1UL, (unsigned long)kLine, slen(kLine));  // write
    // The hand-off: when the glibc station's image rides on the disk,
    // exec chains into it - one program replacing another replacing a
    // third. When it does not (no host toolchain built it), this
    // program simply keeps the seat and exits.
    const long kNext_fd = sys3(5UL, (unsigned long)kNext, 0UL, 0UL);  // open
    if (kNext_fd >= 0) {
        sys3(6UL, (unsigned long)kNext_fd, 0UL, 0UL);                 // close
        sys3(11UL, (unsigned long)kNext, 0UL, 0UL);                   // execve
    }
    sys3(1UL, 0UL, 0UL, 0UL);                           // exit 0
}
