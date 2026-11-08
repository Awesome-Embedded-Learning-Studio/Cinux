# TLB shootdown design note

Status: not implemented. This note records what exists, why the local
`invlpg` is wrong on SMP, and the shape a shootdown should take when
someone picks it up.

## What exists today

`cinux::arch::InvalidatePage` (kernel/arch/x86_64/registers.hpp) is a
single `invlpg`, so only the CPU that edits a leaf entry flushes its
own translation. The page-table editors call it after every leaf
rewrite: demand fill, copy-on-write, munmap, mprotect, loader mapping.

The one existing cross-CPU primitive is the reschedule IPI:

- `smp::kRescheduleVector = 0xE0` (kernel/arch/x86_64/smp_config.hpp).
- `smp::NotifyRunnable` (kernel/arch/x86_64/smp.cpp) sends it to the
  other CPUs with `LocalApic::send(0, kRescheduleVector | mask)`, where
  the mask bits 18 and 19 mean all-but-self.
- `local_entry<kRescheduleVector>` (kernel/arch/x86_64/irq_stubs.cpp)
  only bumps `PerCpu::reschedule_interrupts` and then calls
  `Scheduler::maybe_preempt`; it does not touch the TLB.

So a wakeup IPI exists; an invalidation IPI does not.

## Why local-only is wrong

Two CPUs can run threads of one CLONE_VM group, so both can have the
same address space loaded. CPU A unmaps a page or downgrades its
permissions; CPU B keeps a stale TLB entry and can read or write a
frame that is already back in the allocator. That is silent memory
corruption, not a crash. The same hazard covers a page replaced by a
different physical frame (remap) — the old translation survives.

Cases that are safe today by accident:

- A CPU switching address spaces reloads CR3, which flushes every
  non-global entry. Without PCID there is no tagged TLB, so a task
  cannot carry another space's stale entry across a switch.
- Uniprocessor builds and the current kernel tests: one CPU, one
  TLB, nothing to broadcast.

The bug needs SMP plus CLONE_VM plus a concurrent unmap/protect. The
kernel already advertises 2-CPU ktest runs, so the combination is not
hypothetical.

## Proposed shape

### Option A: conservative full flush (smallest correct step)

1. Add `std::atomic<unsigned int> tlb_flush_epoch` to `PerCpu`.
2. Add `arch::smp::FlushOtherTlbs()`: bump the requester's own epoch,
   then send `kRescheduleVector` to all-but-self (reuse the existing
   mask and `LocalApic::send`).
3. In `local_entry<kRescheduleVector>`, after the counter bump, reload
   CR3 (`ReadCr3` then `LoadCr3`) when the local epoch is behind the
   global one. A CR3 reload drops every non-global entry of the space
   this CPU runs, which is over-flush but always safe.
4. Call `FlushOtherTlbs()` from the syscall-level editors (munmap,
   mprotect) after they finish their leaf rewrites, and from the
   demand/CoW paths only when frames were actually remapped.

Cost: one IPI per editor call, and a full TLB drain on receivers
instead of one page. Fine for a teaching kernel; wrong for a database.

### Option B: address-targeted shootdown

1. Keep a per-space `cpumask` of CPUs currently running that address
   space, maintained in `LoadCr3`-adjacent code (the place that knows
   the outgoing and incoming root).
2. A requester stores `(root, virtual_address, epoch)` in a small
   mailbox per target CPU, bumps that CPU's mail epoch, sends the IPI,
   and spins until every target acknowledges (a counter per epoch).
3. The receiver takes the mailbox under a spin lock, does
   `invlpg(address)` when `root` equals its current root, acknowledges.
4. Mailboxes must be one word plus an address with a single-writer
   discipline; a lost update means a lost shootdown, so the requester
   owns the slot until acknowledged.

Option B needs per-space CPU tracking that does not exist yet, and a
second mailbox word to avoid the ABA of two pending addresses. That is
the reason it is a note.

## Constraints any version must respect

- Never send an IPI or spin for an acknowledgement with a spinlock
  held that the receiver might want: the fault path holds the VMA hold
  (`VmaStore::Hold`) and `AddressSpace::guard()` around table edits,
  so a shootdown called from inside those must be done after dropping
  them, or the receiver must not need those locks to acknowledge.
  The acknowledgement path should touch only the per-CPU mailbox.
- Interrupts off around the send (`IrqGuard`), same as
  `NotifyRunnable`.
- PCID, if it is ever enabled, breaks the "CR3 reload flushes"
  argument: global entries and tagged entries survive. The kernel does
  not enable PCID today; re-check `CR4.PCIDE` before relying on the
  reload.
- The requester must not wait forever on an offline CPU: check
  `OnlineCpuCount` and the online flags, as `NotifyRunnable` does.

## Verification plan

- The host tests cannot exercise this: no second CPU and no real TLB.
- A 2-CPU ktest can assert bookkeeping (epochs, mailboxes, mask
  maintenance) but not that a stale entry was really gone.
- Real confidence needs bare metal or a TCG run with a deliberate
  remap-under-concurrent-access loop and a checksum over the frame.

## Files that would change

- kernel/arch/x86_64/per_cpu.hpp: epoch or mailbox.
- kernel/arch/x86_64/smp.cpp/.hpp: send and acknowledge logic.
- kernel/arch/x86_64/irq_stubs.cpp: the reschedule handler.
- kernel/mm/demand.cpp, kernel/syscall/sys_munmap.cpp,
  kernel/syscall/sys_mprotect.cpp: request after the leaf rewrite.
