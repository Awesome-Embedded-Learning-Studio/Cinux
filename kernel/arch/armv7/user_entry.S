@ armv7 drop to user mode: save the kernel's callee-saved world, switch
@ mode, set the user stack while in it, and branch to the entry.
@ r0 = entry, r1 = user stack top, r2 = kernel context to resume.

.syntax unified
.arm
.section .text.user_entry, "ax"
.global EnterUserModeWithReturn

EnterUserModeWithReturn:
    stmia   r2, {r4-r11, sp, lr}
    cpsid   if
    cps     #0x10
    mov     sp, r1
    bx      r0
