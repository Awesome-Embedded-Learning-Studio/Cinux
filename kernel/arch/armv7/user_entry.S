@ armv7 drop to user mode.
@ r0 = entry, r1 = user stack top, r2 = kernel context to resume.
@
@ The user stack lives in the user bank, so it is set from System mode,
@ which shares that bank and is still privileged. The drop itself is an
@ exception return: SPSR says user mode, lr says where. Doing the mode
@ switch first and then continuing here would fetch PL1-only pages in
@ user mode and fault, which is exactly what the first version did.

.syntax unified
.arm
.section .text.user_entry, "ax"
.global EnterUserModeWithReturn

EnterUserModeWithReturn:
    stmia   r2, {r4-r11, sp, lr}
    mov     r3, #0x10               @ user mode CPSR
    cps     #0x1F                   @ system mode: the user bank's sp
    mov     sp, r1
    cps     #0x13                   @ back to supervisor
    msr     spsr_cxsf, r3
    mov     lr, r0
    movs    pc, lr                  @ exception return into user mode
