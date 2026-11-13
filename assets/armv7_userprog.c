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

static long sys6(unsigned long number, unsigned long a, unsigned long b, unsigned long c,
                  unsigned long d, unsigned long e, unsigned long f) {
    register long r0 __asm__("r0") = (long)a;
    register long r1 __asm__("r1") = (long)b;
    register long r2 __asm__("r2") = (long)c;
    register long r3 __asm__("r3") = (long)d;
    register long r4 __asm__("r4") = (long)e;
    register long r5 __asm__("r5") = (long)f;
    register long r7 __asm__("r7") = (long)number;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                     : "memory");
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
    // The heap: extend brk, touch the fresh page (a not-present fault
    // the neutral pager fills), and read the words back.
    const unsigned long kBase    = (unsigned long)sys3(45UL, 0UL, 0UL, 0UL);
    const unsigned long kWanted  = (kBase + 0xFFFFUL) & ~0xFFFUL;
    const unsigned long kBrk     = (unsigned long)sys3(45UL, kWanted, 0UL, 0UL);
    static const char kBrkA[] = "[guest] brk refused\n";
    static const char kBrkB[] = "[guest] brk ok, heap page reads back\n";
    if (kBrk != kWanted) {
        sys3(4UL, 1UL, (unsigned long)kBrkA, slen(kBrkA));
    } else {
        volatile unsigned int* heap = (volatile unsigned int*)(kWanted - 0x800UL);
        heap[0] = 0x600D600DU;
        heap[1] = 0xC0DEC0DEU;
        if (heap[0] == 0x600D600DU && heap[1] == 0xC0DEC0DEU) {
            sys3(4UL, 1UL, (unsigned long)kBrkB, slen(kBrkB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kBrkA, slen(kBrkA));
        }
    }
    // Anonymous mmap: the pager fills this one page by page on touch.
        const long kMap = sys6(90UL, 0UL, 0x1000UL, 0x3UL /* PROT_RW */,
                           0x22UL /* MAP_PRIVATE|MAP_ANON */, ~0UL, 0UL);
    static const char kMapA[] = "[guest] mmap refused\n";
    static const char kMapB[] = "[guest] mmap ok, anon page reads back\n";
    if (kMap < 0) {
        sys3(4UL, 1UL, (unsigned long)kMapA, slen(kMapA));
    } else {
        volatile unsigned char* anon = (volatile unsigned char*)kMap;
        anon[0]      = 0x5AU;
        anon[0x0FFU] = 0xA5U;
        // The promise held: both touches were filled by the pager.
        const long kUn = sys3(91UL, (unsigned long)kMap, 0x1000UL, 0UL);
        if (kUn == 0) {
            sys3(4UL, 1UL, (unsigned long)kMapB, slen(kMapB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kMapA, slen(kMapA));
        }
    }
    // Pty round trip: ask the master for its slave number, open the
    // slave by name, and hear the echo both ways.
    {
        static const char kPtyA[] = "[guest] pty refused\n";
        static const char kPtyB[] = "[guest] pty ok, master and slave echo\n";
        const long kMaster = sys3(5UL, (unsigned long)"/dev/ptmx", 2UL, 0UL);
        if (kMaster < 0) {
            sys3(4UL, 1UL, (unsigned long)kPtyA, slen(kPtyA));
        } else {
            unsigned int number = 0xFFFFU;
            const long kAsk =
                sys3(54UL, (unsigned long)kMaster, 0x80045430UL,
                     (unsigned long)&number);  // ioctl TIOCGPTN
            if (kAsk != 0 || number >= 4U) {
                sys3(4UL, 1UL, (unsigned long)kPtyA, slen(kPtyA));
            } else {
                static const char kSlaveA[] = "/dev/pts/0";
                static const char kSlaveB[] = "/dev/pts/1";
                static const char kSlaveC[] = "/dev/pts/2";
                static const char kSlaveD[] = "/dev/pts/3";
                const char* kSlave = kSlaveA;
                if (number == 1U) {
                    kSlave = kSlaveB;
                } else if (number == 2U) {
                    kSlave = kSlaveC;
                } else if (number == 3U) {
                    kSlave = kSlaveD;
                }
                const long kSlaveFd = sys3(5UL, (unsigned long)kSlave, 2UL, 0UL);
                if (kSlaveFd < 0) {
                    sys3(4UL, 1UL, (unsigned long)kPtyA, slen(kPtyA));
                } else {
                    static const char kHello[] = "ping\n";
                    sys3(4UL, (unsigned long)kMaster, (unsigned long)kHello, 5UL);
                    char back[5];
                    const long kGot =
                        sys3(3UL, (unsigned long)kSlaveFd, (unsigned long)back, 5UL);
                    if (kGot == 5 && back[0] == 'p' && back[3] == 'g') {
                        sys3(4UL, 1UL, (unsigned long)kPtyB, slen(kPtyB));
                    } else {
                        sys3(4UL, 1UL, (unsigned long)kPtyA, slen(kPtyA));
                    }
                    sys3(6UL, (unsigned long)kSlaveFd, 0UL, 0UL);
                }
            }
            sys3(6UL, (unsigned long)kMaster, 0UL, 0UL);
        }
    }
    // Blocking stdin: the line the runner drips into the UART arrives
    // through the console tty's line discipline, canonical and echoed.
    {
        static const char kLiA[] = "[guest] stdin refused\n";
        static const char kLiB[] = "[guest] stdin ok, line read back\n";
        char line[16];
        for (unsigned long i = 0; i < sizeof(line); ++i) {
            line[i] = 0;
        }
        const long kGot = sys3(3UL, 0UL, (unsigned long)line, sizeof(line) - 1UL);
        if (kGot == 6 && line[0]=='c'&&line[1]=='i'&&line[2]=='n'&&line[3]=='u'&&line[4]=='x'&&line[5]=='\n') {
            sys3(4UL, 1UL, (unsigned long)kLiB, slen(kLiB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kLiA, slen(kLiA));
        }
    }
    // The code page is read-only: a write faults, and the seat's cow
    // seam must hand back a private writable copy before the readback.
    {
        static const char kRoA[] = "[guest] code page stayed read-only\n";
        static const char kRoB[] = "[guest] cow seat ok, code write repaired\n";
        volatile unsigned char* kPad = (volatile unsigned char*)0x40000FF0UL;
        *kPad = 0xA5;
        sys3(4UL, 1UL, (unsigned long)(*kPad == 0xA5 ? kRoB : kRoA),
             *kPad == 0xA5 ? slen(kRoB) : slen(kRoA));
    }
    // Nonblocking stdin: F_SETFL arms O_NONBLOCK, and the empty
    // line answers EAGAIN instead of parking the caller.
    {
        static const char kNbA[] = "[guest] fcntl refused\n";
        static const char kNbB[] = "[guest] fcntl ok, empty stdin answers EAGAIN\n";
        const long kArmed = sys3(55UL, 0UL, 4UL, 0x800UL);  // fcntl F_SETFL O_NONBLOCK
        char back[8];
        const long kGot = sys3(3UL, 0UL, (unsigned long)back, sizeof(back));  // read stdin
        if (kArmed == 0 && kGot == -11L) {
            sys3(4UL, 1UL, (unsigned long)kNbB, slen(kNbB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kNbA, slen(kNbA));
        }
    }
    // fstat: the motd's shape, regular file with its byte count.
    {
        static const char kStA[] = "[guest] fstat refused\n";
        static const char kStB[] = "[guest] fstat ok, motd is a regular 9-byte file\n";
        struct GuestStat {
            unsigned long long dev;
            unsigned long long ino;
            unsigned long long links;
            unsigned int       mode;
            unsigned int       uid;
            unsigned int       gid;
            unsigned int       padding;
            unsigned long long rdev;
            long long          size;
            long long          blksize;
            long long          blocks;
            unsigned long long atime;
            unsigned long long mtime;
            unsigned long long ctime;
            unsigned int       unused[4];
        } stat;
        const long kMotd = sys3(5UL, (unsigned long)"/motd", 0UL, 0UL);
        const long kSt   = sys3(108UL, (unsigned long)kMotd, (unsigned long)&stat, 0UL);
        if (kSt == 0 && stat.mode == 0x81A4U && stat.size == 9) {
            sys3(4UL, 1UL, (unsigned long)kStB, slen(kStB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kStA, slen(kStA));
        }
        sys3(6UL, (unsigned long)kMotd, 0UL, 0UL);
    }
    // Directory walk: the root's records list the seeded files back,
    // hello included now that the old world releases what it holds.
    {
        static const char kDlA[] = "[guest] getdents refused\n";
        static const char kDlB[] = "[guest] getdents ok, root holds five entries with hello\n";
        const long kRoot = sys3(5UL, (unsigned long)"/", 0UL, 0UL);
        unsigned char records[512];
        unsigned long names = 0;
        unsigned long saw_hello = 0;
        long got = 0;
        if (kRoot >= 0) {
            got = sys3(217UL, (unsigned long)kRoot, (unsigned long)records, sizeof(records));
            unsigned long walk = 0;
            while (got > 0 && walk + 19 < (unsigned long)got) {
                const unsigned short kLen =
                    *(const unsigned short*)(records + walk + 16);
                const char* kName = (const char*)(records + walk + 19);
                ++names;
                if (kName[0]=='h'&&kName[1]=='e'&&kName[2]=='l'&&kName[3]=='l'&&kName[4]=='o'&&kName[5]=='\0') {
                    saw_hello = 1;
                }
                walk += kLen;
            }
        }
        if (got > 0 && names == 5 && saw_hello) {
            sys3(4UL, 1UL, (unsigned long)kDlB, slen(kDlB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kDlA, slen(kDlA));
        }
        sys3(6UL, (unsigned long)kRoot, 0UL, 0UL);
    }
    // Fork: the child proves COW (its write stays private) and exits
    // with a word, the parent reaps it through wait4 and checks both
    // the identity and the exit code.
    {
        static const char kForkA[] = "[guest] fork refused\n";
        static const char kForkB[] = "[guest] fork ok, child reaped with own page\n";
        volatile unsigned long* shared =
            (volatile unsigned long*)(unsigned long)sys6(
                90UL, 0UL, 0x1000UL, 0x3UL, 0x22UL, ~0UL, 0UL);  // mmap the duel page
        *shared = 0x1111UL;
        const long kPid = sys3(2UL, 0UL, 0UL, 0UL);  // fork
        if (kPid < 0) {
            sys3(4UL, 1UL, (unsigned long)kForkA, slen(kForkA));
        } else if (kPid == 0) {
            // Child: the duel page must start as the parent left it,
            // and this side's write must not need anyone's leave.
            const unsigned long kVerdict =
                (*shared == 0x1111UL) ? 7UL : 8UL;
            *shared = 0x2222UL;
            sys3(1UL, kVerdict, 0UL, 0UL);  // exit(kVerdict)
            for (;;) {
            }
        } else {
            unsigned int status = 0xFFFFU;
            const long kReaped = sys3(114UL, (unsigned long)kPid,
                                      (unsigned long)&status, 0UL);  // wait4
            if (kReaped == kPid && status == 7U && *shared == 0x1111UL) {
                sys3(4UL, 1UL, (unsigned long)kForkB, slen(kForkB));
            } else {
                sys3(4UL, 1UL, (unsigned long)kForkA, slen(kForkA));
            }
        }
    }
    sys3(1UL, 0UL, 0UL, 0UL);
    for (;;) {
    }
}
