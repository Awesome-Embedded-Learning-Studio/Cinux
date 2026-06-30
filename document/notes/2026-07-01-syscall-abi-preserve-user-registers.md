# 2026-07-01 syscall ABI: preserve Linux syscall-preserved user registers

> Correction: this note originally claimed the busybox `-O2` smoke was fully
> fixed.  That was wrong.  Restoring Linux syscall-preserved GPRs is necessary,
> but it was not sufficient for the mallocng `a_crash`.  The remaining root
> cause is documented in `2026-07-01-syscall-simd-state-preserve.md`.

## Symptom

With musl 1.2.6 built at `-O2` and busybox built static via musl-gcc, busybox
could enter musl mallocng's `a_crash` path:

```text
#GP user mode at rip=0x406a66
```

`0x406a66` is musl's x86_64 `hlt` trap in `a_crash`, used as a heap-integrity
alarm.  `-O0` happened to avoid the failure, while optimized builds exposed it.

## Root Cause Addressed Here

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
Linux's raw syscall ABI.  Optimized user code may keep live state in those
registers across inline syscalls, so returning clobbered values is invalid.

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

## Superseded Validation

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

The busybox binary used for the later reproduced failure was:

```text
md5(build/musl/busybox) = 993475f67490ef73e262466050ff27b9
md5(/tmp/busybox/busybox) = 993475f67490ef73e262466050ff27b9
```

Commands:

```sh
timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)
timeout 40 cmake --build build --target run-kernel-test -j$(nproc)
cmake --build build --target test_host -j$(nproc)
python3 scripts/check_freestanding_headers.py
```

The PASS result originally recorded here was a false positive: sandboxed QEMU
failed to create its VNC/socket, so the guest smoke did not really execute.
When re-run as a real QEMU execution with optimized busybox, this GPR-only fix
still failed:

```text
[ERROR] #GP user mode ... rip=0x0000000000406A66 ... -- sending SIGILL
[F-ECO] busybox echo 0/5 PASS -> FAIL
```

The GPR restore remains correct and is kept, but the successful busybox
validation belongs to the later SIMD/FPU entry-state fix.
