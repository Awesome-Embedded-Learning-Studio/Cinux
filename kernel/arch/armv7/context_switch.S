@ armv7 cooperative switch: save the ten callee-saved words into r0's
@ image, load them from r1's, and return into whichever world was loaded.

.syntax unified
.arm
.section .text.context_switch, "ax"
.global ContextSwitch

ContextSwitch:
    stmia   r0, {r4-r11, sp, lr}
    ldmia   r1, {r4-r11, sp, lr}
    bx      lr
