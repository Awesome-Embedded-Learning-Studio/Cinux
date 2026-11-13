@ armv7 exception vectors. Every slot branches to a stub that names the
@ vector; IRQ and the trapping svc share one trap-frame save and one
@ resume, so a preempted task and a yielding task are described the same.

.syntax unified
.arm
.section .vectors, "ax"
.align 5
.global _vectors
_vectors:
    b   _vec0       @ reset, never taken here
    b   _vec1       @ undefined instruction
    b   _vec2       @ supervisor call
    b   _vec3       @ prefetch abort
    b   _vec4       @ data abort
    b   _vec5       @ reserved
    b   _vec6       @ IRQ
    b   _vec7       @ FIQ

_vec0: mov r0, #0
       b   _dispatch
_vec1: mov r0, #1
       b   _dispatch
@ Prefetch abort: a user-mode fetch miss is served exactly like a
@ user data miss - the page arrives, the instruction runs again. Only
@ a kernel-mode fetch miss goes to the dump.
_vec3:  stmdb sp!, {r3}
        mrs   r3, spsr
        and   r3, r3, #0x1F
        cmp   r3, #0x10              @ only a user fetch may be repaired
        bne   _prefetch_plain
        ldmia sp!, {r3}
        b     _prefetch_trap_save
_prefetch_plain:
        add   sp, sp, #4
        b     _prefetch_dump
_vec4: sub  lr, lr, #8
       stmdb sp!, {r3}            @ park r3 while the mode is checked
       mrs  r3, spsr
       and  r3, r3, #0x1F
       cmp  r3, #0x10              @ only a user fault may be repaired
       bne  _abort_plain
       ldmia sp!, {r3}
       b    _abort_trap_save
_abort_plain:
       add  sp, sp, #4
       mov  r0, #4
       b    _dispatch
_vec5: mov r0, #5
       b   _dispatch
_vec7: sub lr, lr, #4
       mov r0, #7
       b   _dispatch

@ Supervisor call: every number takes the trapping path, so the tables in
@ svc.cpp are the only place that knows which numbers exist.
_vec2: b _svc_trap

@ IRQ: the return address needs the four-byte correction before saving.
_vec6: sub  lr, lr, #4
       stmdb sp!, {r3}             @ park r3 while the mode is checked
       mrs  r3, spsr
       and  r3, r3, #0x1F
       cmp  r3, #0x10              @ only a user interrupt may preempt
       bne  _irq_plain
       ldmia sp!, {r3}
       b    _irq_trap_save
_irq_plain:
       add  sp, sp, #4
       push {r0-r3, lr}            @ the kernel's own registers survive
       mov  r0, #6
       bl   c_exception_dispatch
       pop  {r0-r3, lr}
       movs pc, lr

@ Trapping svc: lr is already the user's return address.
_svc_trap:

@ Save the interrupted world onto this episode's own stack, let C
@ to resume, then restore it. r12 is parked on the supervisor stack
@ because it is both the base and one of the saved registers.
@ The frame rides this episode's own stack, shaped as a UserTrap:
@ r0-r12 first, then the user's sp, pc, and SPSR. A blocked handler
@ leaves the frame parked where the interrupted task's kernel stack
@ keeps it safe while another task runs its own episode.
_svc_trap_save:
       sub   sp, sp, #12           @ room for the user's sp/pc/SPSR
       stmdb sp!, {r0-r12}         @ r0@0 .. r12@48
       cps   #0x1F                 @ System mode shares the user sp bank
       mov   r1, sp
       cps   #0x13                 @ back to supervisor mode
       mov   r2, lr                @ the user's return address
       mrs   r3, spsr
       str   r1, [sp, #52]         @ user sp
       str   r2, [sp, #56]         @ user pc
       str   r3, [sp, #60]         @ user SPSR
       mov   r0, sp
       mov   r1, #1                @ supervisor call
       bl    c_trap_entry
       mov   r3, r0
       ldr   r1, [r3, #52]         @ user sp
       ldr   r2, [r3, #60]         @ SPSR
       ldr   r12, [r3, #56]        @ user pc
       cps   #0x1F
       mov   sp, r1
       cps   #0x13
       msr   spsr_cxsf, r2
       mov   lr, r12
       ldmia r3, {r0-r12}
       add   sp, sp, #64           @ the episode's frame comes off
       movs  pc, lr

@ Same stack frame shape as the svc path, on the IRQ bank's own stack:
@ the episode is LIFO here, and the resumed task's image comes from
@ the frame pointer the C side returns.
_irq_trap_save:
       sub   sp, sp, #12           @ room for the user's sp/pc/SPSR
       stmdb sp!, {r0-r12}         @ r0@0 .. r12@48
       cps   #0x1F                 @ System mode shares the user sp bank
       mov   r1, sp
       cps   #0x12                 @ back to IRQ mode
       mov   r2, lr                @ the user's resume address
       mrs   r3, spsr
       str   r1, [sp, #52]         @ user sp
       str   r2, [sp, #56]         @ user pc
       str   r3, [sp, #60]         @ user SPSR
       mov   r0, sp
       mov   r1, #0                @ interrupt
       bl    c_trap_entry
       mov   r3, r0
       add   sp, sp, #64           @ free the frame on this bank's stack
       ldr   r1, [r3, #52]         @ user sp
       ldr   r2, [r3, #60]         @ SPSR
       ldr   r12, [r3, #56]        @ user pc
       cps   #0x1F
       mov   sp, r1
       cps   #0x13
       msr   spsr_cxsf, r2
       mov   lr, r12
       ldmia r3, {r0-r12}
       movs  pc, lr

@ Same stack frame shape as the svc path, on the abort bank's stack.
_abort_trap_save:
       sub   sp, sp, #12           @ room for the user's sp/pc/SPSR
       stmdb sp!, {r0-r12}         @ r0@0 .. r12@48
       cps   #0x1F                 @ System mode shares the user sp bank
       mov   r1, sp
       cps   #0x17                 @ back to abort mode
       mov   r2, lr                @ the faulting instruction
       mrs   r3, spsr
       str   r1, [sp, #52]         @ user sp
       str   r2, [sp, #56]         @ user pc
       str   r3, [sp, #60]         @ user SPSR
       mov   r0, sp
       mov   r1, #2
       bl    c_trap_entry
       mov   r3, r0
       add   sp, sp, #64           @ free the frame on this bank's stack
       ldr   r1, [r3, #52]         @ user sp
       ldr   r2, [r3, #60]         @ SPSR
       ldr   r12, [r3, #56]        @ user pc
       cps   #0x1F
       mov   sp, r1
       cps   #0x13
       msr   spsr_cxsf, r2
       mov   lr, r12
       ldmia r3, {r0-r12}
       movs  pc, lr

@ A kernel-mode prefetch abort: dump the kernel world and stop. The
@ frame shape matches the abort save path so C can read it the same way.
@ A user-mode prefetch abort: same frame shape as the data-abort
@ save, taken in the prefetch-abort bank. lr already names the
@ faulting instruction, so resuming re-executes it uncorrected.
_prefetch_trap_save:
       sub   sp, sp, #12           @ room for the user's sp/pc/SPSR
       stmdb sp!, {r0-r12}         @ r0@0 .. r12@48
       cps   #0x1F                 @ System mode shares the user sp bank
       mov   r1, sp
       cps   #0x1B                 @ back to prefetch-abort mode
       mov   r2, lr                @ the faulting instruction
       mrs   r3, spsr
       str   r1, [sp, #52]         @ user sp
       str   r2, [sp, #56]         @ user pc
       str   r3, [sp, #60]         @ user SPSR
       mov   r0, sp
       mov   r1, #3
       bl    c_trap_entry
       mov   r3, r0
       add   sp, sp, #64
       ldr   r1, [r3, #52]         @ user sp
       ldr   r2, [r3, #60]         @ SPSR
       ldr   r12, [r3, #56]        @ user pc
       cps   #0x1F
       mov   sp, r1
       cps   #0x13
       msr   spsr_cxsf, r2
       mov   lr, r12
       ldmia r3, {r0-r12}
       movs  pc, lr

_prefetch_dump:
       sub   sp, sp, #12
       stmdb sp!, {r0-r12}
       cps   #0x13
       mov   r1, sp
       cps   #0x17
       str   r1, [sp, #52]
       mov   r2, lr
       str   r2, [sp, #56]
       mrs   r3, spsr
       str   r3, [sp, #60]
       mov   r0, sp
       bl    c_kernel_prefetch_dump
1:     b     1b

@ Everything else: report the slot and stop.
_dispatch:
    push    {r0-r3, lr}
    bl      c_exception_dispatch
    pop     {r0-r3, lr}
    movs    pc, lr
