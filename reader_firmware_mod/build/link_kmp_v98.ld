MEMORY { HOOKS (rx) : ORIGIN = 0xEE08, LENGTH = 0x1000 }
ENTRY(kmp_tramp)
SECTIONS { .text : { KEEP(*(.text.kmp_tramp)) *(.text*) *(.rodata*) } > HOOKS
  /DISCARD/ : { *(.ARM.*) *(.comment) *(.note*) } }
