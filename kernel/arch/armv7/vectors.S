@ armv7 exception vectors: only IRQ is serviced during bring-up; every other
@ entry parks, which is louder than a silent return to a broken frame.

.syntax unified
.arm
.section .vectors, "ax"
.align 5
.global _vectors
_vectors:
    b   _park       @ reset, never taken here
    b   _park       @ undefined instruction
    b   _park       @ supervisor call
    b   _park       @ prefetch abort
    b   _park       @ data abort
    b   _park       @ reserved
    b   _irq_entry  @ IRQ
    b   _irq_entry  @ FIQ (a secure-state core signals group 0 here)

_irq_entry:
    sub     lr, lr, #4
    push    {r0-r3, lr}
    bl      c_irq_handler
    pop     {r0-r3, lr}
    movs    pc, lr

_park:
    wfi
    b       _park
