/* armv7 bring-up image: linked where the i.MX6ULL boot ROM and QEMU agree
   to place a kernel, with a 16 KiB stack that costs no image bytes. */
ENTRY(_start)

/* One PT_LOAD for the whole image. Letting the linker guess split the
   .bss and the stacks into a second segment whose file size is zero,
   and QEMU's -kernel loader then started a machine with no output. */
PHDRS
{
    image PT_LOAD FLAGS(7);
}

SECTIONS
{
    . = 0x80000000;

    .vectors : {
        KEEP(*(.vectors))
    } :image
    .text : {
        KEEP(*(.text.start))
        *(.text*)
    } :image
    .rodata : {
        . = ALIGN(8);
        /* objcopy leaves the font blob at alignment 1; keep it word
           aligned so the PSF2 header reads do not fault. */
        KEEP(*(.rodata.font))
        *(.rodata*)
    } :image
    .data   : { *(.data*) } :image

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :image

    .stack (NOLOAD) : ALIGN(16) {
        . += 16K;
        __stack_top = .;
    } :image

    .irq_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __irq_stack_top = .;
    } :image

    .fiq_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __fiq_stack_top = .;
    } :image

    .abt_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __abt_stack_top = .;
    } :image

    .und_stack (NOLOAD) : ALIGN(8) {
        . += 4K;
        __und_stack_top = .;
    } :image
}
