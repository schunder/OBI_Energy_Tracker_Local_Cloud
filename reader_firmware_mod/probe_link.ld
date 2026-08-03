/* probe_link.ld — places the read-session state probe in the free flash right after
 * reader_stock_v57.bin (44552 B @ base 0x4000 -> next free byte 0xEE08), exactly like
 * link.ld does for the production hooks. Same region, same proven-executable address.
 */
MEMORY
{
  HOOKS (rx) : ORIGIN = 0xEE08, LENGTH = 0x1000
}

ENTRY(entry_probe)

SECTIONS
{
  .text : {
    KEEP(*(.text.entry_probe))
    *(.text*)
    *(.rodata*)
  } > HOOKS

  /DISCARD/ : {
    *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.ARM.attributes*)
  }
}
