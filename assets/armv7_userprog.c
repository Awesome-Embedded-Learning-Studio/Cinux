// The bring-up lane's first real guest: one write through the console
// face, one clean exit, both by supervisor call. Built freestanding,
// linked at the port's user window, seated by the neutral loader.
static long sys3(unsigned long number, unsigned long a, unsigned long b,
                 unsigned long c) {
    register long r0 __asm__("r0") = (long)a;
    register long r1 __asm__("r1") = (long)b;
    register long r2 __asm__("r2") = (long)c;
    register long r7 __asm__("r7") = (long)number;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

void guest_main(void) {
    static const char kGreeting[] = "[guest] hello from the loaded image\n";
    sys3(4UL, 1UL, (unsigned long)kGreeting, sizeof(kGreeting) - 1U);
    sys3(1UL, 0UL, 0UL, 0UL);
    for (;;) {
    }
}
