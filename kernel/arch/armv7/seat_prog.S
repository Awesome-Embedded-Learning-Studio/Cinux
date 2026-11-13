@ The scheduler-seat probe's user program: one write through the
@ console face, one return-to-kernel service call. It runs on a
@ private user page built by user_seat.cpp; the numbers it passes in
@ r7 are the same faces the loaded guest uses.

.syntax unified
.arm

.section .text.seat_prog, "ax"
.global SeatProgStart
.global SeatProgEnd

SeatProgStart:
    adr    r1, seat_msg
    movw   r0, #1
    movw   r2, #40
    movw   r7, #4                 @ write
    svc    #0
    ldr    r7, =0x1000            @ return to the waiting kernel body
    svc    #0
    b      .                      @ never reached

seat_msg:
    .ascii "[seat] scheduler task writes from user mode\n"
seat_msg_end:
    .align 2
SeatProgEnd:
