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
_vec4: sub lr, lr, #8
       mov r0, #4
       b   _dispatch
_vec5: mov r0, #5
       b   _dispatch
_vec7: sub lr, lr, #4
       mov r0, #7
       b   _dispatch

@ Supervisor call, ordinary path: number in r7, arguments in r0-r3.
_vec2: cmp  r7, #158
       beq  _svc_trap
       cmp  r7, #1
       beq  _svc_trap
       push {r0-r3, lr}
       mov  r0, r7
       mov  r1, sp
       bl   c_svc_handler
       str  r0, [sp]
       pop  {r0-r3, lr}
       movs pc, lr

@ IRQ: the return address needs the four-byte correction before saving.
_vec6: sub  lr, lr, #4
       mov  r1, #0                 @ interrupt
       mov  r2, #0xD2              @ back to IRQ mode, interrupts masked
       b    _trap_save

@ Trapping svc: lr is already the user's return address.
_svc_trap:
       mov  r1, #1                 @ supervisor call
       mov  r2, #0xD3              @ back to supervisor mode, interrupts masked

@ Save the interrupted world into g_user_trap, let C choose the frame
@ to resume, then restore it. r12 is parked on the supervisor stack
@ because it is both the base and one of the saved registers.
_trap_save:
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
       msr   cpsr_c, r2            @ back to the mode we came from
       str   r3, [r12, #52]
       mov   r0, r12
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
