# 2026-07-01 syscall/ISR: preserve user SIMD state across kernel entry

## Symptom

After `a90cc4a` restored Linux syscall-preserved GPRs, optimized busybox still
failed in musl mallocng:

```text
[ERROR] #GP user mode: ... rip=0x0000000000406A66 ... -- sending SIGILL
[F-ECO] busybox echo 0/5 PASS -> FAIL
```

The binary matched the local repro target:

```text
md5(build/musl/busybox) = 993475f67490ef73e262466050ff27b9
md5(/tmp/busybox/busybox) = 993475f67490ef73e262466050ff27b9
```

## Root Cause

`0x406A66` is musl mallocng's `a_crash` (`hlt`) in `queue()`, reached because a
fresh `struct meta` still had non-zero `prev/next`.  A temporary crash probe
showed those fields contained ASCII from the kernel's `[VMM] Demand-paged ...
phys ...` log line, for example:

```text
meta[0] = 0x3030303030303030
meta[1] = 0x3030303031423130
```

The important disassembly chain is:

```text
__malloc_alloc_meta:
  pxor   %xmm0,%xmm0
  ...
  syscall / page fault into CinuxOS
  ...
  movups %xmm0,(%r10)      ; intended to clear meta->prev/meta->next

alloc_slot:
  cmpq   $0x0,0x8(%r11)
  hlt
```

CinuxOS preserved GPRs, but did not preserve the user FPU/SSE state on
`SYSCALL` or interrupt/exception entry.  Kernel C++ and logging code could use
`xmm0`; on return to musl, its still-live zero vector had become kernel
formatting bytes.  The later `movups %xmm0,(%r10)` wrote those bytes into
mallocng metadata instead of zeroing it.

## Fix

`syscall_entry` now reserves a 512-byte FXSAVE area below the syscall frame,
saves the user FPU/SSE image before calling C++, refreshes `Task::fpu_state`,
and restores the stack copy immediately before `SYSRETQ`.

All ISR stubs now do the same around C/C++ handlers.  This protects:

- user SIMD state across page faults, IRQs, and signal delivery;
- kernel SIMD temporaries across asynchronous interrupts;
- preempted user tasks, by refreshing `Task::fpu_state` when the entry came from
  CPL=3.

`ret_from_fork` restores the child task's inherited `fpu_state` before returning
to user mode.  `Task::fpu_state` offset is pinned with a `static_assert` because
the assembly uses that offset directly.

The anonymous demand-page path also now zero-fills recycled PMM pages before
mapping them into user space.  That was not the sufficient root cause for this
crash, but it is required for correct anonymous `brk`/`MAP_ANON`/stack
semantics and prevents stale kernel/user data leaks.

## Validation

Configuration:

```sh
cmake -B build \
  -DCINUX_BUSYBOX_SMOKE=ON \
  -DCINUX_MUSL_HELLO_SMOKE=OFF \
  -DCINUX_MUSL_DYN_SMOKE=OFF \
  -S .
```

Real QEMU run (not sandbox-false-green):

```sh
timeout 60 cmake --build build --target run-kernel-test -j$(nproc)
```

Result:

```text
[TEST] ALL TESTS PASSED (exit code 0)
[F-ECO] busybox echo 5/5 PASS -> PASS
[F-ECO] busybox ls PASS (status=0 reap=10)
[100%] Built target run-kernel-test
```

