/*
 * The glibc station's hello: a real toolchain product, statically
 * linked, speaking through the raw write syscall the kernel serves.
 *
 * Built (outside the tree, host toolchain dependent):
 *   arm-none-linux-gnueabihf-gcc -mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 \
 *     -mfloat-abi=hard -static -O2 -nostdlib? NO - glibc needs its crt.
 *   arm-none-linux-gnueabihf-gcc -mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 \
 *     -mfloat-abi=hard -static -O2 -o armv7_glibc_hello.elf armv7_glibc_hello.c
 */

#include <unistd.h>

int main(void) {
    static const char kLine[] = "[glibc] hello from ext2, real libc ran\n";
    if (write(STDOUT_FILENO, kLine, sizeof(kLine) - 1U) != (ssize_t)(sizeof(kLine) - 1U)) {
        _exit(1);
    }
    return 0;
}
