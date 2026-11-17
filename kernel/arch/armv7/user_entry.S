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
    cps     #0x1F                   @ System mode: r14 is the user's
    mov     r3, lr
    cps     #0x13
    str     r3, [r2, #44]           @ the kernel half keeps the user lr
    mov     r3, #0x10               @ user mode CPSR
    tst     r0, #1                  @ a Thumb entry carries its mark in
    orrne   r3, r3, #0x20           @ bit 0: the T bit must follow it
    bic     r0, r0, #1              @ the pc itself is even
    cps     #0x1F                   @ system mode: the user bank's sp
    mov     sp, r1
    cps     #0x13                   @ back to supervisor
    msr     spsr_cxsf, r3
    mov     lr, r0
    movs    pc, lr                  @ exception return into user mode

.global ResumeTrap
ResumeTrap:
    mov     r3, r0
    ldr     r1, [r3, #52]           @ user sp
    ldr     r2, [r3, #60]           @ SPSR
    ldr     r12, [r3, #56]          @ user pc
    cps     #0x1F
    mov     sp, r1
    ldr     lr, [r3, #64]
    cps     #0x13
    msr     spsr_cxsf, r2
    mov     lr, r12
    ldmia   r3, {r0-r12}
    movs    pc, lr

@ SeatForkChildEntry helper: save the caller's eleven words into the
@ seat body image, then fall into ResumeTrap on the copied parent
@ frame. The saved lr is the word after this call, so a park that
@ switches back to the body resumes right here like any seated body.
.global SaveContextResumeTrap
SaveContextResumeTrap:
    stmia   r0, {r4-r11, sp, lr}
    mrs     r2, spsr
    str     r2, [r0, #40]
    cps     #0x1F
    mov     r2, lr
    cps     #0x13
    str     r2, [r0, #44]
    mov     r0, r1
    b       ResumeTrap
