# 2026-07-01 syscall ABI: preserve Linux syscall-preserved user registers

> F-ECO busybox O2 follow-up.  Root cause of the musl `-O1/-O2` corruption was
> not `brk`/`mmap` return values themselves, but CinuxOS returning from syscall
> with argument registers clobbered by the kernel C dispatcher.

## Symptom

With musl 1.2.6 built at `-O2` and busybox built static via musl-gcc, busybox
could enter musl mallocng's `a_crash` path:

```text
#GP user mode at rip=0x406a66
```

`0x406a66` is musl's x86_64 `hlt` trap in `a_crash`, used as a heap-integrity
alarm.  `-O0` happened to avoid the failure, while optimized builds exposed it.

## Root Cause

musl's x86_64 syscall wrappers use inline asm matching the Linux syscall ABI:

```c
__asm__ volatile("syscall" : "=a"(ret)
                 : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                 : "rcx", "r11", "memory");
```

That contract says:

- `rax` returns the syscall result.
- `rcx` and `r11` are clobbered by the `SYSCALL/SYSRET` mechanism.
- Other user-visible general registers, including `rdi/rsi/rdx/r10/r8/r9`, are
  preserved across the syscall instruction.

CinuxOS captured those argument registers in the syscall frame on entry, but the
return path only restored SysV callee-saved registers (`r12-r15/rbx/rbp`).  The
call to `syscall_dispatch` is a normal C call, so it may freely clobber
caller-saved registers.  Returning those clobbered values to user mode violated
Linux's raw syscall ABI.  Optimized musl kept live state in those registers
across inline syscalls, then reused corrupted values, leading to allocator
metadata damage and the later mallocng trap.

This also explains why function-call-shaped or unoptimized paths were much less
sensitive: the compiler already treats ordinary C calls as clobbering
caller-saved registers.

## Fix

`kernel/arch/x86_64/syscall.S` now restores all Linux syscall-preserved user
registers from the 128-byte syscall frame before `SYSRETQ`:

```text
rdi rsi rdx r10 r8 r9 r12 r13 r14 r15 rbx rbp
```

`rax` is still restored from the per-CPU return-value scratch, and `rcx/r11`
remain the architected `SYSRETQ` inputs.  `ret_from_fork` mirrors the same
restore set so fork children resume user mode with the same syscall ABI, except
for `rax=0`.

No trap-frame size change was needed; the missing registers were already saved
at syscall entry.

## Validation

The local build was configured with:

```text
CINUX_BUSYBOX_SMOKE=ON
CINUX_MUSL_HELLO_SMOKE=OFF
CINUX_MUSL_DYN_SMOKE=OFF
```

The current musl sysroot is an optimized one:

```text
build/musl/musl-1.2.6/config.mak: CFLAGS = -O2 -g3 -fno-omit-frame-pointer
```

The current busybox binary was linked with musl-gcc and busybox `-Oz -static`.

Commands:

```sh
timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)
timeout 40 cmake --build build --target run-kernel-test -j$(nproc)
cmake --build build --target test_host -j$(nproc)
python3 scripts/check_freestanding_headers.py
```

Results:

- `run-kernel-test-all`: PASS, single-CPU then `-smp 2`.
- `run-kernel-test`: PASS with busybox smoke enabled.
- `test_host`: 69/69 PASS.
- freestanding header gate: PASS.

## Notes

`build/debug.log` still contained an older pre-fix `#GP @ 0x406a66` diagnostic
line from 2026-07-01 00:21; it predates the successful reruns and is stale.

