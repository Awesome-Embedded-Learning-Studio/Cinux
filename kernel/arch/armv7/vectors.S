@ armv7 exception vectors: every slot branches to a stub that names the
@ vector and then joins the one C dispatch. The table itself stays eight
@ four-byte branches, as the architecture requires.

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
_vec2: cmp  r7, #158
       beq  _svc_sched
       cmp  r7, #1
       beq  _svc_sched
       push {r0-r3, lr}
       mov  r0, r7
       mov  r1, sp
       bl   c_svc_handler
       str  r0, [sp]
       pop  {r0-r3, lr}
       movs pc, lr

@ Trapping svc: save the user's world into a frame the scheduler can
@ move, let C pick the next one, then restore that frame through an
@ exception return. The user's sp lives in the user bank, so it is read
@ and written from System mode.
_svc_sched:
       ldr  r12, =g_user_save_area
       stmia r12, {r4-r11}
       cps  #0x1F
       mov  r3, sp
       cps  #0x13
       str  r3, [r12, #32]
       str  lr, [r12, #36]
       mrs  r3, spsr
       str  r3, [r12, #40]
       mov  r0, r7
       ldr  r1, =g_user_save_area
       bl   c_schedule_from_user
       ldr  r1, [r0, #32]
       ldr  r2, [r0, #40]
       ldr  lr, [r0, #36]
       cps  #0x1F
       mov  sp, r1
       cps  #0x13
       msr  spsr_cxsf, r2
       ldmia r0, {r4-r11}
       movs pc, lr
_vec3: mov r0, #3
       b   _dispatch
_vec4: sub lr, lr, #8
       mov r0, #4
       b   _dispatch
_vec5: mov r0, #5
       b   _dispatch
_vec6: sub lr, lr, #4
       mov r0, #6
       b   _dispatch
_vec7: sub lr, lr, #4
       mov r0, #7
       b   _dispatch

_dispatch:
    push    {r0-r3, lr}
    bl      c_exception_dispatch
    pop     {r0-r3, lr}
    movs    pc, lr
