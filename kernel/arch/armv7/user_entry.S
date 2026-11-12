@ armv7 user-side entry and frame restore.
@
@ EnterUserModeWithReturn: r0 = entry, r1 = user stack top,
@ r2 = kernel context to resume. The user stack lives in the user bank,
@ so it is set from System mode; the drop is an exception return.
@
@ user_entry_stub: the copied user text starts here. It reads the task's
@ argument from the top of its own stack before any C prologue can move
@ the pointer, then hands it to the C body.
@
@ ResumeUserFrame: r0 = frame. Called from SVC mode; restores the frame
@ and returns to user mode.

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

.global ResumeUserFrame
ResumeUserFrame:
    ldr     r1, [r0, #32]           @ user sp
    ldr     r2, [r0, #40]           @ SPSR
    ldr     lr, [r0, #36]           @ user pc
    cps     #0x1F
    mov     sp, r1
    cps     #0x13
    msr     spsr_cxsf, r2
    ldmia   r0, {r4-r11}
    movs    pc, lr

.section .user_text, "ax"
.global user_entry_stub

user_entry_stub:
    ldr     r0, [sp, #-4]           @ the task's argument, from its own stack
    b       user_program_c
