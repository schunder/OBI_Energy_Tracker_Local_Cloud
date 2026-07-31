MEMORY { HOOKS (rx) : ORIGIN = 0xEE08, LENGTH = 0x1000 }
ENTRY(kmp_tramp)
SECTIONS {
  .text : {
    KEEP(*(.text.kmp_tramp))
    KEEP(*(.text.kmp_arm)) KEEP(*(.text.kmp_poll)) KEEP(*(.text.kmp_rx_byte)) KEEP(*(.text.kmp_decode))
    *(.text*) *(.rodata*)
  } > HOOKS
  /DISCARD/ : { *(.ARM.*) *(.comment) *(.note*) }
}
