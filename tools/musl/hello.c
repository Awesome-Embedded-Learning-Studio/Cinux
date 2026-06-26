/*
 * hello.c — minimal musl static program for CinuxOS (F10-M1 batch 5).
 *
 * Linked against the self-contained musl sysroot (see build-musl.sh).  This is
 * the smoke binary for the F10-M1 user-runtime arc: batch 5 produces it on the
 * host, batch 6 loads it via execve + the ELF loader + the batch-3 initial
 * stack and runs it under QEMU, where printf routes through the batch-4
 * writev/arch_prctl syscalls.
 */
#include <stdio.h>

int main(void) {
    printf("Hello from musl on CinuxOS!\n");
    return 0;
}
