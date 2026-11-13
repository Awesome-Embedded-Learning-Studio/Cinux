// The bring-up lane's guest process: a real file citizen. It opens a
// file from the volume, reads it, writes it to the console, closes,
// and exits — every step through the neutral syscall bodies the x86
// lane built, seated on the real scheduler.
static long sys3(unsigned long number, unsigned long a, unsigned long b,
                 unsigned long c) {
    register long r0 __asm__("r0") = (long)a;
    register long r1 __asm__("r1") = (long)b;
    register long r2 __asm__("r2") = (long)c;
    register long r7 __asm__("r7") = (long)number;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static unsigned long slen(const char* s) {
    unsigned long n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

void guest_main(void) {
    static const char kTag[] = "[guest] hello from the loaded image\n";
    sys3(4UL, 1UL, (unsigned long)kTag, slen(kTag));

    static char page[4096] __attribute__((aligned(16)));
    const long kFd = sys3(5UL, (unsigned long)"/motd", 0UL, 0UL);  // open
    if (kFd >= 0) {
        const long kGot = sys3(3UL, (unsigned long)kFd, (unsigned long)page,
                               sizeof(page) - 1UL);  // read
        if (kGot > 0) {
            page[kGot] = 0;
            static const char kSays[] = "[guest] motd: ";
            sys3(4UL, 1UL, (unsigned long)kSays, slen(kSays));
            sys3(4UL, 1UL, (unsigned long)page, (unsigned long)kGot);
        } else {
            static const char kBad[] = "[guest] motd read failed\n";
            sys3(4UL, 1UL, (unsigned long)kBad, slen(kBad));
        }
        sys3(6UL, (unsigned long)kFd, 0UL, 0UL);  // close
    } else {
        static const char kNo[] = "[guest] motd open failed\n";
        sys3(4UL, 1UL, (unsigned long)kNo, slen(kNo));
    }
    sys3(1UL, 0UL, 0UL, 0UL);
    for (;;) {
    }
}
