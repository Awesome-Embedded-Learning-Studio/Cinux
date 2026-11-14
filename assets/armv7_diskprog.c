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
    sys3(4UL, 1UL, (unsigned long)kLine, slen(kLine));  // write
    sys3(1UL, 0UL, 0UL, 0UL);                           // exit 0
}
