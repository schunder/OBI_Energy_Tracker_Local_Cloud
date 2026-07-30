MEMORY { HOOKS (rx) : ORIGIN = 0xEE08, LENGTH = 0x1000 }
ENTRY(kmp_poll)
SECTIONS {
  .text : {
    KEEP(*(.text.kmp_poll)) KEEP(*(.text.kmp_rx_byte)) KEEP(*(.text.kmp_decode)) KEEP(*(.text.kmp_crc))
    *(.text*) *(.rodata*)
  } > HOOKS
  /DISCARD/ : { *(.ARM.*) *(.comment) *(.note*) }
}
