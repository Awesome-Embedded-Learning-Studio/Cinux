SECTIONS
{
  . = 0x40000000;
  .text : { *(.text*) *(.rodata*) }
  . = 0x40002000;
  .persistent : { *(.data*) *(.bss*) *(COMMON) }
  /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
