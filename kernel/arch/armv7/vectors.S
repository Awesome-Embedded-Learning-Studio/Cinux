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
_vec3: mov r0, #3
       b   _dispatch
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

@ Save the interrupted world into g_user_trap, let C choose the frame
@ to resume, then restore it. r12 is parked on the supervisor stack
@ because it is both the base and one of the saved registers.
_svc_trap_save:
       stmdb sp!, {r12}
       ldr   r12, =g_user_trap
       stmia r12, {r0-r11}
       ldr   r3, [sp], #4
       str   r3, [r12, #48]
       str   lr, [r12, #56]        @ the trapping mode's lr
       mrs   r3, spsr
       str   r3, [r12, #60]        @ the trapping mode's SPSR
       cps   #0x1F                 @ System mode reads the user bank's sp
       mov   r3, sp
       cps   #0x13                 @ back to supervisor mode
       str   r3, [r12, #52]
       mov   r0, r12
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
       movs  pc, lr

_irq_trap_save:
       stmdb sp!, {r12}
       ldr   r12, =g_user_trap
       stmia r12, {r0-r11}
       ldr   r3, [sp], #4
       str   r3, [r12, #48]
       str   lr, [r12, #56]        @ the trapping mode's lr
       mrs   r3, spsr
       str   r3, [r12, #60]        @ the trapping mode's SPSR
       cps   #0x1F                 @ System mode reads the user bank's sp
       mov   r3, sp
       cps   #0x12                 @ back to IRQ mode
       str   r3, [r12, #52]
       mov   r0, r12
       mov   r1, #0                @ interrupt
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
       movs  pc, lr

_abort_trap_save:
       stmdb sp!, {r12}
       ldr   r12, =g_user_trap
       stmia r12, {r0-r11}
       ldr   r3, [sp], #4
       str   r3, [r12, #48]
       str   lr, [r12, #56]        @ the trapping mode's lr
       mrs   r3, spsr
       str   r3, [r12, #60]        @ the trapping mode's SPSR
       cps   #0x1F                 @ System mode reads the user bank's sp
       mov   r3, sp
       cps   #0x13                 @ back to supervisor mode
       str   r3, [r12, #52]
       mov   r0, r12
       mov   r1, #2
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
       movs  pc, lr

@ Everything else: report the slot and stop.
_dispatch:
    push    {r0-r3, lr}
    bl      c_exception_dispatch
    pop     {r0-r3, lr}
    movs    pc, lr
