@ armv7 entry: the first instruction after the boot ROM hands over.
@ Interrupts off, a stack of our own, VFP on, BSS cleared, then kmain.

.syntax unified
.arm
.section .text.start, "ax"
.global _start

_start:
    cpsid   aif
    @ CPACR: enable CP10/CP11 (VFP/NEON) before any float instruction runs.
    mrc     p15, 0, r0, c1, c0, 2
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    @ FPEXC.EN gates execution itself: CPACR only opens the coprocessor's
    @ door, and without this bit every vldr the compiler emits for moving
    @ 64-bit constants faults as an undefined instruction.
    mov     r0, #(1 << 30)
    vmsr    fpexc, r0
    isb

    @ Each exception mode owns a stack; IRQ/FIQ land here, not on the boot stack.
    cps     #0x12               @ IRQ mode
    ldr     sp, =__irq_stack_top
    cps     #0x11               @ FIQ mode
    ldr     sp, =__fiq_stack_top
    cps     #0x17               @ abort mode
    ldr     sp, =__abt_stack_top
    cps     #0x1B               @ undefined mode: nothing may ride a
    ldr     sp, =__und_stack_top @ garbage sp into a frame ever again
    cps     #0x13               @ back to supervisor

    ldr     sp, =__stack_top
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      kmain
2:  wfi
    b       2b
