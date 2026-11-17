/*
 * The glibc station's program: a real toolchain product, statically
 * linked, exercising the libc surface a program actually uses — argv,
 * the allocator, formatted output, and a disk read through libc's own
 * file descriptors.
 *
 * Built by scripts/build-armv7-glibc-prog.sh:
 *   arm-none-linux-gnueabihf-gcc -mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 \
 *     -mfloat-abi=hard -static -no-pie -fno-pie -O2 \
 *     -Wl,-Ttext-segment=0x40010000 -o assets/armv7_glibc_hello.elf <this>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <time.h>

#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

// Exercise the temporary ppoll mask on real ARM, then both kernel
// trampolines. BusyBox additionally covers Thumb handlers/restorers.
static volatile sig_atomic_t caught, temporary_mask_ok;
static void signal_probe(int number) {
    sigset_t mask;
    if (number == SIGUSR1 && sigprocmask(SIG_SETMASK, NULL, &mask) == 0) {
        temporary_mask_ok = !sigismember(&mask, SIGUSR2);
        ++caught;
    }
}
static void signal_info_probe(int number, siginfo_t* info, void* context) {
    if (number == SIGUSR1 && info && info->si_signo == number && context) ++caught;
}
static int signal_roundtrip(void) {
    struct sigaction action = {0}, before;
    sigset_t blocked, saved, empty, after;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGUSR2);
    sigemptyset(&empty);
    action.sa_handler = signal_probe;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, &before) ||
        sigprocmask(SIG_BLOCK, &blocked, &saved) || kill(getpid(), SIGUSR1)) return 1;
    errno = 0;
    long result = syscall(SYS_ppoll, NULL, 0, NULL, &empty, 8);
    if (result != -1 || errno != EINTR || caught != 1 || !temporary_mask_ok ||
        sigprocmask(SIG_SETMASK, NULL, &after) ||
        !sigismember(&after, SIGUSR1) || !sigismember(&after, SIGUSR2)) return 2;
    struct timespec zero = {0};
    if (syscall(SYS_ppoll, NULL, 0, &zero, &empty, 8) != 0 || caught != 1) return 3;
    if (sigprocmask(SIG_SETMASK, &saved, NULL)) return 4;
    // Wire-format action without SA_RESTORER forces the kernel stubs.
    struct { uint32_t handler, flags, restorer, mask[2]; } raw = {0};
    raw.handler = (uintptr_t)signal_info_probe;
    raw.flags = SA_SIGINFO;
    if (syscall(SYS_rt_sigaction, SIGUSR1, &raw, NULL, 8) ||
        kill(getpid(), SIGUSR1) || caught != 2) return 5;
    raw.handler = (uintptr_t)signal_probe;
    raw.flags = 0;
    if (syscall(SYS_rt_sigaction, SIGUSR1, &raw, NULL, 8) ||
        kill(getpid(), SIGUSR1) || caught != 3) return 6;
    if (sigaction(SIGUSR1, &before, NULL)) return 7;
    puts("[glibc] signal ok: ppoll EINTR once, temporary mask, sigreturn and rt_sigreturn");
    return 0;
}

int main(int argc, char** argv) {
    int signal_result = signal_roundtrip();
    if (signal_result) {
        printf("[glibc] signal FAILED %d errno %d\n", signal_result, errno);
        return 20 + signal_result;
    }
    // The stdio face, first: a lock or buffer that only exists there
    // would show up before anything else the station does.
    {
        FILE* const kFile = fopen("/motd", "r");
        char        buffer[64] = {};
        if (kFile == NULL) {
            static const char kNoFile[] = "[glibc] fopen refused\n";
            (void)!write(STDOUT_FILENO, kNoFile, sizeof(kNoFile) - 1U);
        } else {
            if (fgets(buffer, sizeof(buffer), kFile) == NULL) {
                buffer[0] = 0;
            }
            fclose(kFile);
            char line[96] = {};
            const int kLen = snprintf(line, sizeof(line), "[glibc] stdio read: %s", buffer);
            if (kLen > 0) {
                (void)!write(STDOUT_FILENO, line, (unsigned long)kLen);
            }
        }
    }
    static const char kLine[] = "[glibc] hello from ext2, real libc ran\n";
    if (write(STDOUT_FILENO, kLine, sizeof(kLine) - 1U) !=
        (ssize_t)(sizeof(kLine) - 1U)) {
        return 1;
    }

    // The allocator: two blocks, written and read back after a free, so
    // a broken arena would show as a wrong byte rather than a crash.
    char* const kFirst = malloc(64);
    char* const kSecond = malloc(256);
    if (kFirst == NULL || kSecond == NULL) {
        return 2;
    }
    strcpy(kFirst, "arena");
    memset(kSecond, 'z', 255);
    kSecond[255] = '\0';
    free(kFirst);
    if (strcmp(kSecond, "zzz") != 0 && strlen(kSecond) != 255) {
        return 3;
    }

    // The disk through libc: the motd the image was molded with.
    const int kFd = open("/etc/motd", O_RDONLY);
    if (kFd < 0) {
        char why[64] = {};
        const int kLen = snprintf(why, sizeof(why), "[glibc] open failed errno %d\n", errno);
        if (kLen > 0) {
            write(STDOUT_FILENO, why, (unsigned long)kLen);
        }
        return 4;
    }
    char motd[128] = {};
    const ssize_t kGot = read(kFd, motd, sizeof(motd) - 1U);
    close(kFd);
    if (kGot <= 0) {
        return 5;
    }

    // Formatted output, and the argument vector the kernel built.
    const char* const kSelf = argc > 0 && argv[0] != NULL ? argv[0] : "?";
    printf("[glibc] malloc, printf, open all ok, argc %d, self %s, motd %ld bytes\n", argc,
           kSelf, (long)kGot);
    fflush(stdout);

    // The GUI street: map the panel's own frame buffer and paint it.
    // The two colours below are the kernel's read-back proof: it looks
    // at the panel's memory after this process leaves.
    {
        static const char kFbLine[] = "[glibc] fb painted, %d bytes mapped\n";
        const int fb = open("/dev/fb0", O_RDWR);
        int painted = 0;
        if (fb >= 0) {
            const unsigned long kBytes = 640UL * 480UL * 4UL;
            unsigned int* const pixels = mmap(NULL, kBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb,
                                              0);
            if (pixels != MAP_FAILED) {
                const unsigned long kProof = (480UL - 4UL) * 640UL + 5UL;
                // What the kernel stamped before this process started:
                // a match proves this mapping is the panel's memory.
                const unsigned int kSeed = pixels[kProof];
                char seed[80] = {};
                const int kSeedLen =
                    snprintf(seed, sizeof(seed), "[glibc] fb seed %X\n", kSeed);
                if (kSeedLen > 0) {
                    write(STDOUT_FILENO, seed, (unsigned long)kSeedLen);
                }
                for (unsigned long i = 0; i < kBytes / 4UL; ++i) {
                    pixels[i] = 0x00102030U;
                }
                pixels[kProof] = 0x00FF00FFU;
                pixels[kProof + 1UL] = 0x0000FF00U;
                msync(pixels, kBytes, MS_SYNC);
                char back[96] = {};
                const int kBackLen = snprintf(back, sizeof(back), "[glibc] fb readback %X %X\n",
                                              pixels[kProof], pixels[kProof + 1UL]);
                if (kBackLen > 0) {
                    write(STDOUT_FILENO, back, (unsigned long)kBackLen);
                }
                painted = (int)kBytes;
                // The mapping stays live through exit on purpose: the
                // console only repaints its own text while a holder has
                // the frame, and the kernel's read-back wants to see
                // these pixels untouched.
            }
        }
        char line[64] = {};
        const int kLen = snprintf(line, sizeof(line), kFbLine, painted);
        if (kLen > 0) {
            write(STDOUT_FILENO, line, (unsigned long)kLen);
        }
    }

    // The hand-off to a real userland: busybox, statically linked, run
    // as its `echo` applet. Success never returns here.
#ifdef CINUX_ARM_PROBE_DESK
    // The demo hand-off: run the desktop itself. The devfs seats its
    // input streams flat under /dev, so the desk gets told where they
    // are instead of guessing Linux's layout. The desk never returns;
    // this mode exists for looking at the screen, not for the check.
    char* const kArgs[] = {(char*)"desk", (char*)"/dev/fb0", (char*)"/dev/event0",
                           (char*)"/dev/event1", NULL};
    // The desk's own event trace is behind this variable: with it set,
    // every word it decodes is printed, which is how a key that reached
    // the desktop gets seen from the serial log.
    char* const kEnv[] = {(char*)"VESPER_FBDEV_TRACE=1", NULL};
    execve("/desk", kArgs, kEnv);
#elif defined(CINUX_ARM_PROBE_INTERACTIVE)
    // The interactive probe: sh with no command line reads from the
    // console itself. A real login hands the shell a controlling
    // terminal first -- own session, ctty claimed, foreground pgrp
    // adopted -- or interactive ash spins waiting to become the
    // foreground group. That hand-off is done here, the same door
    // getty uses, before the shell takes the seat.
    setsid();
    ioctl(0, TIOCSCTTY, 0);
    char* const kArgs[] = {(char*)"sh", NULL};
    execve("/bin/sh", kArgs, NULL);
#else
    // The shell itself: one command line proving the parser, a child
    // applet through fork-and-exec, a builtin echo, a file read, and
    // then the same process becomes a bare interactive shell -- the
    // typed line answers the read, the next two land in the bare
    // shell's command loop. The ctty claim is the login's door: the
    // interactive half spins without it.
    setsid();
    ioctl(0, TIOCSCTTY, 0);
    char* const kArgs[] = {(char*)"sh", (char*)"-c",
                           (char*)"stat /prog; echo shell ok; cat /readme; "
                                   "read line; echo typed:$line; exec sh", NULL};
    execve("/bin/sh", kArgs, NULL);
#endif
    static const char kNoBb[] = "[glibc] sh exec refused\n";
    write(STDOUT_FILENO, kNoBb, sizeof(kNoBb) - 1U);
    return 0;
}
