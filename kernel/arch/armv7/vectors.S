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
_vec3:  sub   lr, lr, #4             @ the fetch reports its address + 4
        stmdb sp!, {r3}
        mrs   r3, spsr
        and   r3, r3, #0x1F
        cmp   r3, #0x10              @ only a user fetch may be repaired
        bne   _prefetch_plain
        ldmia sp!, {r3}
        b     _prefetch_trap_save
_prefetch_plain:
        add   sp, sp, #4
        add   lr, lr, #4
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
       ldmia sp!, {r3}             @ hand the interrupted world its r3 back
       push {r0-r3, r12, lr}       @ the kernel's own registers survive
       mov  r0, #6
       bl   c_exception_dispatch
       pop  {r0-r3, r12, lr}
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
@ the frame pointer the C side returns. A seated task takes the other
@ door below: its episodes may park mid-frame while the scheduler
@ lends the CPU elsewhere, and that switch must happen on the task's
@ own supervisor stack, never on the one-per-CPU bank.
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
        mov   r0, sp                @ the frame, LIFO on this bank's top
        ldr   r1, =c_seat_irq_door  @ no C before the frame exists: a call
        ldr   r1, [r1]              @ would eat the user's r0-r3, lr
        cmp   r1, #0
        bne   _irq_seated_rehome
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

@ The seated IRQ door, frame-homed like the abort path: the episode's
@ frame moves onto the interrupted task's supervisor stack, the bank
@ rebalances, and only then does C run - because the tick may switch
@ this task away mid-frame and the switch must save a stack the task
@ owns. The handler returns the same frame; a preempted task simply
@ arrives back here later and resumes the interrupted user code.
_irq_seated_rehome:                 @ r0 = the frame on the IRQ bank's stack
@ rehome: copy the 64-byte frame to the task's supervisor stack
        cps   #0x13                 @ supervisor: sp = the task's kernel sp
        sub   sp, sp, #64
        mov   r4, sp
        add   r5, sp, #64
1:      ldmia r0!, {r2, r3}
        stmia r4!, {r2, r3}
        cmp   r4, r5
        blo   1b
        mov   r0, sp                @ r0 = the frame in its final home
        cps   #0x12
        add   sp, sp, #64           @ the IRQ bank comes back to its top
        cps   #0x13
        bl    c_irq_trap_seated
        mov   r3, r0                @ same frame, maybe much later
        add   sp, sp, #64           @ the episode's frame comes off
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

@ Same stack frame shape as the svc path, built on the abort bank's
@ stack only long enough to move it: the abort bank is one per CPU,
@ so a handler that parks or dies mid-frame would abandon it there
@ and the bank stack would drift until it hits bottom. The frame
@ therefore moves onto the interrupted task's supervisor stack - the
@ same home the svc path parks its frames on - and the bank stack
@ comes back to its top before any C runs.
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
       mov   r1, #2                @ data abort
       b     _banked_trap_run

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
       cps   #0x17                 @ back to abort mode: prefetch and
                                    @ data aborts share the one bank
       mov   r2, lr                @ the faulting instruction
       mrs   r3, spsr
       str   r1, [sp, #52]         @ user sp
       str   r2, [sp, #56]         @ user pc
       str   r3, [sp, #60]         @ user SPSR
       mov   r1, #3                @ fetch abort
       b     _banked_trap_run

@ Shared by the data- and fetch-abort stubs. On arrival the frame is
@ complete on the abort bank's stack and r1 carries the trap kind.
@ r0-r12 hold user values here, but the resume path reloads every one
@ of them from the frame copy, so the copy may clobber them freely.
_banked_trap_run:
       mov   r0, sp                @ r0 = the bank-frame to move
       cps   #0x13                 @ supervisor: sp = the task's kernel sp
       sub   sp, sp, #64
       mov   r4, sp                @ r4 = the frame's new home
       add   r5, sp, #64           @ r5 = one past the frame's end
1:     ldmia r0!, {r2, r3}
       stmia r4!, {r2, r3}
       cmp   r4, r5
       blo   1b                    @ sixteen words shuttled, 64 bytes done
       mov   r0, sp                @ r0 = the frame on the task stack
       cps   #0x17
       add   sp, sp, #64           @ the bank stack is whole again
       cps   #0x13
       bl    c_trap_entry          @ C runs on the task's own stack
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
       add   sp, sp, #64           @ the frame comes off the task stack
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
