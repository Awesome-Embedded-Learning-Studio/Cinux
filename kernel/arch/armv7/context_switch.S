@ armv7 cooperative switch: save the eleven words into r0's image,
@ including SPSR, load them from r1's, and return into whichever world
@ was loaded. SPSR is what makes a switch out of an svc handler resume
@ in user mode instead of the supervisor's mode.

.syntax unified
.arm
.section .text.context_switch, "ax"
.global ContextSwitch

@ The thread pointer rides along: TPIDRURO is per CPU, not per task,
@ so it is saved into the image and reloaded with the rest. Its slot is
@ the word after the two 64-bit segment bases, at byte offset 64.
@ r14_usr rides along too, in the word at offset 44: r14 is banked per
@ mode, and the supervisor's lr belongs to the switch itself, so a
@ process that entered the kernel through a service call would hand the
@ next world's user lr to its caller on the way back. It is read and
@ written in System mode, where r14 names the user bank.
ContextSwitch:
    stmia   r0, {r4-r11, sp, lr}
    mrs     r2, spsr
    str     r2, [r0, #40]
    cps     #0x1F                   @ System: sp/lr are the user bank's
    mov     r2, lr
    cps     #0x13
    str     r2, [r0, #44]
    mrc     p15, 0, r2, c13, c0, 3
    str     r2, [r0, #64]
    ldr     r2, [r1, #40]
    msr     spsr_cxsf, r2
    ldr     r3, [r1, #64]
    mcr     p15, 0, r3, c13, c0, 3
    ldr     r2, [r1, #44]
    cps     #0x1F
    mov     lr, r2
    cps     #0x13
    ldmia   r1, {r4-r11, sp, lr}
    bx      lr

@ Same save/load, but the return is an exception return: the loaded SPSR
@ decides the mode, so a task started through this path runs in user mode.

.global ContextSwitchIntoUser
ContextSwitchIntoUser:
    stmia   r0, {r4-r11, sp, lr}
    mrs     r2, spsr
    str     r2, [r0, #40]
    cps     #0x1F
    mov     r2, lr
    cps     #0x13
    str     r2, [r0, #44]
    mrc     p15, 0, r2, c13, c0, 3
    str     r2, [r0, #64]
    ldr     r2, [r1, #40]
    msr     spsr_cxsf, r2
    ldr     r3, [r1, #64]
    mcr     p15, 0, r3, c13, c0, 3
    ldr     r2, [r1, #44]
    cps     #0x1F
    mov     lr, r2
    cps     #0x13
    ldmia   r1, {r4-r11, sp, lr}
    movs    pc, lr

@ ReadUserLr: answers the user bank's r14. A fork copies it into the
@ child's context so the child returns from the service call that made
@ it to the same place the parent will.

.global ReadUserLr
ReadUserLr:
    cps     #0x1F
    mov     r0, lr
    cps     #0x13
    bx      lr

@ A fresh task starts here through lr, with its body in r4 and its exit
@ hook in r5. A returning body still lands in the unified exit path.

.global TaskTrampoline
TaskTrampoline:
    blx r4
    bx  r5
