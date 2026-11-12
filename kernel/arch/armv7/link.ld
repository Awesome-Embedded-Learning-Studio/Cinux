/* armv7 bring-up image: linked where the i.MX6ULL boot ROM and QEMU agree
   to place a kernel, with a 16 KiB stack that costs no image bytes. */
ENTRY(_start)
SECTIONS
{
    . = 0x80000000;

    .vectors : {
        KEEP(*(.vectors))
    }
    .text : {
        KEEP(*(.text.start))
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16) {
        . += 16K;
        __stack_top = .;
    }

    .irq_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __irq_stack_top = .;
    }

    .fiq_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __fiq_stack_top = .;
    }
}
