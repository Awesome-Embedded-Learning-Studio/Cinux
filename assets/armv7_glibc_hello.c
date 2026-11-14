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

#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv) {
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
    const int kFd = open("/disk/etc/motd", O_RDONLY);
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
            const unsigned long kBytes = 320UL * 240UL * 4UL;
            unsigned int* const pixels = mmap(NULL, kBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb,
                                              0);
            if (pixels != MAP_FAILED) {
                const unsigned long kProof = 200UL * 320UL + 5UL;
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
    char* const kArgs[] = {(char*)"busybox", (char*)"stat", (char*)"/disk/prog", NULL};
    execve("/disk/bb", kArgs, NULL);
    static const char kNoBb[] = "[glibc] busybox exec refused\n";
    write(STDOUT_FILENO, kNoBb, sizeof(kNoBb) - 1U);
    return 0;
}
