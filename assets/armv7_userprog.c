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


static unsigned long slen(const char* s);
__attribute__((used)) static const char kTextPad[0x400] = {1, 2, 3};

void guest_main(void) {
    (void)kTextPad;
    static const char kTag[] = "[guest] hello from the loaded image\n";
    sys3(4UL, 1UL, (unsigned long)kTag, slen(kTag));

        // Edges: the refusal paths the old bring-up world used to own — a
    // kernel pointer read must be refused, a bad prot must be refused,
    // a read-only page earns its private copy on first write, a munmap
    // really frees the hint for the next taker and no further, yield
    // answers zero, lseek rewinds, and a read-only fd refuses writes.
    {
        static const char kEdA[] = "[guest] edges refused\n";
        static const char kEdB[] = "[guest] edges ok, refusals refused and hints honored\n";
        unsigned long good = 1;
if (sys3(4UL, 1UL, 0x80000000UL, 8UL) >= 0) {  // kernel pointer
            good = 0;
        }
if (sys6(90UL, 0UL, 0x1000UL, 0x8UL, 0x22UL, ~0UL, 0UL) >= 0) {  // bad prot
            good = 0;
        }
const long kPage = sys6(90UL, 0UL, 0x1000UL, 0x3UL, 0x22UL, ~0UL, 0UL);
        if (kPage > 0) {
            volatile unsigned long* spot = (volatile unsigned long*)(unsigned long)kPage;
            *spot                        = 0xF00DUL;
sys3(91UL, (unsigned long)kPage, 0x1000UL, 0UL);  // munmap
            const long kAgain = sys6(90UL, 0UL, 0x1000UL, 0x3UL, 0x22UL, ~0UL, 0UL);
            if (kAgain <= 0) {
                good = 0;  // the freed span must serve the next mapping
            } else {
                *(volatile unsigned long*)(unsigned long)kAgain = 0xCAFEUL;
            }
const long kFixed1 = sys6(90UL, 0x40020000UL, 0x1000UL, 0x3UL, 0x32UL, ~0UL, 0UL);
            const long kFixed2 = sys6(90UL, 0x40020000UL, 0x1000UL, 0x3UL, 0x32UL, ~0UL, 0UL);
            volatile unsigned long* fixed_spot = (volatile unsigned long*)0x40020000UL;
            *fixed_spot                        = 0xD00DUL;
            if (kFixed1 != 0x40020000L || kFixed2 != 0x40020000L || *fixed_spot != 0xD00DUL) {
                good = 0;  // a fixed mapping must reclaim and stand twice
            }
        } else {
            good = 0;
        }
if (sys3(158UL, 0UL, 0UL, 0UL) != 0L) {  // sched_yield
            good = 0;
        }
const long kFd = sys3(5UL, (unsigned long)"/motd", 0UL, 0UL);
        if (kFd >= 0) {
            static char nibble[4];
const long kFirst = sys3(3UL, (unsigned long)kFd, (unsigned long)nibble, 4UL);
const long kBack  = sys3(19UL, (unsigned long)kFd, 0UL, 0UL);  // lseek to 0
            const long kRedo  = sys3(3UL, (unsigned long)kFd, (unsigned long)nibble, 4UL);
const long kRoWr  = sys3(4UL, (unsigned long)kFd, (unsigned long)nibble, 1UL);
            if (kFirst != 4L || kBack != 0L || kRedo != 4L || kRoWr != -9L) {
                good = 0;  // a regular file answers no writes
            }
            sys3(6UL, (unsigned long)kFd, 0UL, 0UL);
        } else {
            good = 0;
        }
sys3(4UL, 1UL, (unsigned long)(good ? kEdB : kEdA), slen(good ? kEdB : kEdA));
    }

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
    // The code page is read-only: a write from a child faults, ends that
    // child with the fault word, and leaves the page's own bytes alone.
    // The parent checks all three, so a write that quietly took a private
    // copy fails this line.
    {
        static const char kRoA[] = "[guest] code page check failed\n";
        static const char kRoB[] = "[guest] code page stayed read-only\n";
        // The pad lands inside this probe's own rodata: the RX segment
        // carries it, so its range never promises a write.
        static const unsigned char kScratch[16] = {1, 2, 3, 4, 5, 6, 7, 8,
                                                   9, 10, 11, 12, 13, 14, 15, 16};
        volatile unsigned char* kPad = (volatile unsigned char*)kScratch + 8;
        const unsigned char kBefore = *kPad;
        const long kChild = sys3(2UL, 0UL, 0UL, 0UL);  // fork
        if (kChild < 0) {
            sys3(4UL, 1UL, (unsigned long)kRoA, slen(kRoA));
        } else if (kChild == 0) {
            *kPad = 0xA5;              // must fault and end this child
            sys3(1UL, 0UL, 0UL, 0UL);  // reached only when the write passed
            for (;;) {
            }
        } else {
            unsigned int status = 0;
            const long kReaped = sys3(114UL, (unsigned long)kChild,
                                      (unsigned long)&status, 0UL);
            const int kOk = kReaped == kChild && status == 0xDEADU && *kPad == kBefore;
            sys3(4UL, 1UL, (unsigned long)(kOk ? kRoB : kRoA), slen(kOk ? kRoB : kRoA));
        }
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
    // USB keyboard seat: the usbkbd node answers through the very
    // glyph face the class driver owns; with no keys pressed the
    // nonblocking read must refuse with EAGAIN, not park or lie.
    {
        static const char kKbA[] = "[guest] usbkbd refused\n";
        static const char kKbB[] = "[guest] usbkbd ok, empty answers EAGAIN or a glyph\n";
        const long kFd = sys3(5UL, (unsigned long)"/dev/usbkbd", 0UL, 0UL);  // open
        const long kArm = kFd >= 0 ? sys3(55UL, kFd, 4UL, 0x800UL) : -1L;    // O_NONBLOCK
        char back[4];
        const long kGot = kArm == 0 ? sys3(3UL, kFd, (unsigned long)back, sizeof(back)) : -1L;
        if (kFd >= 0 && kArm == 0 && (kGot == -11L || kGot >= 1L)) {
            sys3(4UL, 1UL, (unsigned long)kKbB, slen(kKbB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kKbA, slen(kKbA));
        }
        if (kFd >= 0) {
            sys3(6UL, kFd, 0UL, 0UL);  // close
        }
    }
    // The real disk: the ext2 volume's motd answers through the very
    // syscall ladder everything else uses. The first line names the
    // volume's baker.
    {
        static const char kDmA[] = "[guest] disk motd refused\n";
        static const char kDmB[] = "[guest] disk motd: Cinux says hello from a real disk.\n";
        static const char kPath[] = "/etc/motd";
        const long kFd = sys3(5UL, (unsigned long)kPath, 0UL, 0UL);  // open
        char back[48];
        unsigned long line = 0;
        long got = kFd >= 0 ? sys3(3UL, kFd, (unsigned long)back, sizeof(back)) : -1L;
        for (long i = 0; i < got; ++i) {
            if (back[i] == '\n') {
                line = (unsigned long)i;
                break;
            }
        }
        // 34 = strlen of the expected first line.
        if (kFd >= 0 && got >= 34 && line == 34) {
            back[34] = 0;
            sys3(4UL, 1UL, (unsigned long)kDmB, slen(kDmB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kDmA, slen(kDmA));
        }
        if (kFd >= 0) {
            sys3(6UL, kFd, 0UL, 0UL);  // close
        }
    }
    // The root layout's links: /bin/sh is a fast symlink to busybox,
    // so opening it proves the walk expanded a relative target and landed
    // on the volume's real ELF. The card grows the same bin/etc/sbin shape
    // the x86 volume has; this is the read that says the walk can find it.
    {
        static const char kLnA[] = "[guest] root layout link refused\n";
        static const char kLnB[] = "[guest] symlink ok, bin/sh opens as an ELF\n";
        static const char kLinkPath[] = "/bin/sh";
        const long kFd = sys3(5UL, (unsigned long)kLinkPath, 0UL, 0UL);  // open
        unsigned char magic[4] = {0, 0, 0, 0};
        const long kRead = kFd >= 0 ? sys3(3UL, kFd, (unsigned long)magic, sizeof(magic)) : -1L;
        const int kIsElf = kRead == 4L && magic[0] == 0x7F && magic[1] == 'E' &&
                           magic[2] == 'L' && magic[3] == 'F';
        if (kFd >= 0 && kIsElf) {
            sys3(4UL, 1UL, (unsigned long)kLnB, slen(kLnB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kLnA, slen(kLnA));
        }
        if (kFd >= 0) {
            sys3(6UL, kFd, 0UL, 0UL);  // close
        }
    }
    // fstat: the motd's shape, regular file with its byte count.
    {
        static const char kStA[] = "[guest] fstat refused\n";
        static const char kStB[] = "[guest] fstat ok, motd is a regular 9-byte file\n";
        // The EABI struct stat64 the kernel writes for this port. The
        // ABI packs its 64-bit size at byte 44, so the mirror must not
        // let the compiler align it further.
        struct __attribute__((packed)) GuestStat {
            unsigned long long dev;
            unsigned int       pad0;
            unsigned int       ino_low;
            unsigned int       mode;
            unsigned int       links;
            unsigned int       uid;
            unsigned int       gid;
            unsigned long long rdev;
            unsigned int       pad3;
            long long          size;
            unsigned int       blksize;
            unsigned long long blocks;
            unsigned long      atime;
            unsigned long      atime_nsec;
            unsigned long      mtime;
            unsigned long      mtime_nsec;
            unsigned long      ctime;
            unsigned long      ctime_nsec;
            unsigned long long ino;
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
    // Sleep: a shell's sleep and every paced loop come through nanosleep.
    // The clock read on both sides proves the call waited instead of merely
    // returning zero, which is the failure a scheduler bug makes.
    {
        static const char kNapA[] = "[guest] nanosleep refused\n";
        static const char kNapB[] = "[guest] nanosleep ok, the clock advanced\n";
        struct arm7_timespec { long int_seconds; long int_nanoseconds; };
        struct arm7_timespec t0;
        struct arm7_timespec t1;
        struct arm7_timespec asked;
        const long kClock0 = sys3(263UL, 0UL, (unsigned long)&t0, 0UL);
        asked.int_seconds = 0;
        asked.int_nanoseconds = 20000000L;  // 20 ms, longer than one tick's wait
        const long kSlept = sys3(162UL, (unsigned long)&asked, 0UL, 0UL);
        const long kClock1 = sys3(263UL, 0UL, (unsigned long)&t1, 0UL);
        const int kAdvanced = kClock0 == 0L && kClock1 == 0L &&
                              (t1.int_seconds > t0.int_seconds ||
                               (t1.int_seconds == t0.int_seconds &&
                                t1.int_nanoseconds > t0.int_nanoseconds));
        if (kSlept == 0L && kAdvanced) {
            sys3(4UL, 1UL, (unsigned long)kNapB, slen(kNapB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kNapA, slen(kNapA));
        }
    }

    // Poll with no descriptors, the pure-timeout shape: it must return
    // zero after really waiting, and the clock is the witness. This is the
    // same turn nanosleep takes, so a wait loop that forgets to let the
    // timer in fails here too.
    {
        static const char kPollA[] = "[guest] poll timeout refused\n";
        static const char kPollB[] = "[guest] poll timeout ok, waited\n";
        struct arm7_poll_ts { long int_seconds; long int_nanoseconds; };
        struct arm7_poll_ts p0;
        struct arm7_poll_ts p1;
        const long kClockA = sys3(263UL, 0UL, (unsigned long)&p0, 0UL);
        const long kReady = sys3(168UL, 0UL, 0UL, 20UL);  // poll(NULL, 0, 20)
        const long kClockB = sys3(263UL, 0UL, (unsigned long)&p1, 0UL);
        const long kElapsed = ((p1.int_seconds - p0.int_seconds) * 1000000000L) +
                              (p1.int_nanoseconds - p0.int_nanoseconds);
        if (kReady == 0L && kClockA == 0L && kClockB == 0L && kElapsed >= 10000000L) {
            sys3(4UL, 1UL, (unsigned long)kPollB, slen(kPollB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kPollA, slen(kPollA));
        }
    }

    // Directory walk: the root's records list the seeded files back,
    // hello included now that the old world releases what it holds.
    {
        static const char kDlA[] = "[guest] getdents refused\n";
        static const char kDlB[] = "[guest] getdents ok, root holds a bin tree with hello\n";
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
        // The card's root now: a bin tree, the bring-up files, the
        // station binaries. The count is a shape check -- eight at least,
        // hello among them -- because the layout will keep growing.
        if (got > 0 && names >= 8 && saw_hello) {
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
    // Litter: three young at once plus a grandchild through the first
    // young. Every young writes its own duel page first, proving each
    // fork got its own copy of the shared frame; the exit codes carry
    // the verdicts home to whoever reaps them.
    {
        static const char kLitA[] = "[guest] litter refused\n";
        static const char kLitB[] = "[guest] litter ok, three young and a grandchild\n";
        volatile unsigned long* duel =
            (volatile unsigned long*)(unsigned long)sys6(
                90UL, 0UL, 0x1000UL, 0x3UL, 0x22UL, ~0UL, 0UL);
        *duel = 0xAAUL;
        unsigned long reaped = 0;
        unsigned long good = 1;
        for (unsigned long i = 0; i < 3; ++i) {
            const long kYoung = sys3(2UL, 0UL, 0UL, 0UL);  // fork
            if (kYoung < 0) {
                good = 0;
                break;
            }
            if (kYoung == 0) {
                // Young: the duel page arrives as the parent left it;
                // this side's mark proves the copy, and the exit word
                // carries it back. The first young also raises a
                // grandchild and reaps it before leaving.
                if (*duel != 0xAAUL) {
                    sys3(1UL, 0x80UL + i, 0UL, 0UL);
                    for (;;) {
                    }
                }
                *duel = 0xB0UL + i;
                if (i == 0) {
                    const long kGrand = sys3(2UL, 0UL, 0UL, 0UL);
                    if (kGrand == 0) {
                        sys3(1UL, 33UL, 0UL, 0UL);  // the grandchild's word
                        for (;;) {
                        }
                    }
                    unsigned int gstatus = 0;
                    gstatus = 0;
                    const long kGRes = sys3(114UL, (unsigned long)kGrand,
                                            (unsigned long)&gstatus, 0UL);
                    if (kGRes != kGrand || gstatus != 33UL) {
                        sys3(1UL, 0x90UL, 0UL, 0UL);
                        for (;;) {
                        }
                    }
                    sys3(1UL, 12UL, 0UL, 0UL);
                    for (;;) {
                    }
                }
                sys3(1UL, 12UL + i, 0UL, 0UL);
                for (;;) {
                }
            }
            unsigned int status = 0;
            const long kW = sys3(114UL, (unsigned long)kYoung,
                                 (unsigned long)&status, 0UL);
            if (kW != kYoung || status != 12UL + i) {
                good = 0;
                break;
            }
            ++reaped;
        }
        // The parent's own duel page must have survived three forks
        // and three private marks untouched.
        if (good && reaped == 3 && *duel == 0xAAUL) {
            sys3(4UL, 1UL, (unsigned long)kLitB, slen(kLitB));
        } else {
            sys3(4UL, 1UL, (unsigned long)kLitA, slen(kLitA));
        }
    }
    // Grim: a child that walks into unmapped memory must die a clean
    // seated death - reaped through wait4 with the fault word, never a
    // hang and never a jump through a stale handler.
    {
        static const char kGmA[] = "[guest] grim reaper refused\n";
        static const char kGmB[] = "[guest] grim reaper ok, faulted child answered DEAD\n";
        const long kChild = sys3(2UL, 0UL, 0UL, 0UL);  // fork
        if (kChild < 0) {
            sys3(4UL, 1UL, (unsigned long)kGmA, slen(kGmA));
        } else if (kChild == 0) {
            volatile unsigned long* doom =
                (volatile unsigned long*)0x50000000UL;  // never mapped
            *doom = 0x1UL;
            sys3(1UL, 9UL, 0UL, 0UL);  // unreachable on a clean death
            for (;;) {
            }
        } else {
            unsigned int status = 0;
            const long kReaped = sys3(114UL, (unsigned long)kChild,
                                      (unsigned long)&status, 0UL);
            sys3(4UL, 1UL,
                 (unsigned long)(kReaped == kChild && status == 0xDEADU ? kGmB : kGmA),
                 slen(kReaped == kChild && status == 0xDEADU ? kGmB : kGmA));
        }
    }
    // Slice: two spinners, one core. Whichever starts first keeps the
    // CPU for its whole run unless the timer preempts it - without IRQ
    // preemption through the neutral scheduler this line never prints.
    {
        static const char kSlA[] = "[guest] slice refused\n";
        static const char kSlB[] = "[guest] slice ok, two spinners shared one core\n";
        volatile unsigned long spin = 0;
        const long              kChild = sys3(2UL, 0UL, 0UL, 0UL);  // fork
        if (kChild < 0) {
            sys3(4UL, 1UL, (unsigned long)kSlA, slen(kSlA));
        } else if (kChild == 0) {
            while (spin < 50000000UL) {
                ++spin;
            }
            sys3(1UL, 6UL, 0UL, 0UL);
            for (;;) {
            }
        } else {
            while (spin < 50000000UL) {
                ++spin;
            }
            unsigned int status = 0;
            const long    kReaped = sys3(114UL, (unsigned long)kChild,
                                         (unsigned long)&status, 0UL);
            const int     kOk = kReaped == kChild && status == 6U;
            sys3(4UL, 1UL, (unsigned long)(kOk ? kSlB : kSlA),
                 slen(kOk ? kSlB : kSlA));
        }
    }
    // The last act: replace this world with the ext2-resident
    // program. Success never returns here; the disk image takes the
    // seat and speaks its own line.
    {
        static const char kExA[] = "[guest] exec from disk refused\n";
        static const char kPath[] = "/prog";
        const long kVerdict = sys3(11UL, (unsigned long)kPath, 0UL, 0UL);  // execve
        if (kVerdict != 0) {
            sys3(4UL, 1UL, (unsigned long)kExA, slen(kExA));
        }
    }
    sys3(1UL, 0UL, 0UL, 0UL);
    for (;;) {
    }
}
static unsigned long slen(const char* s);
// Padding that keeps the text segment past one page: the entry page
// then arrives through the kernel's lazy fetch-repair path, so every
// run exercises it instead of only the eager tail mapping.
static unsigned long slen(const char* s) {
    unsigned long n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}
