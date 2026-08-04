/* probe_link.ld
 *
 * SIZE IS SAFETY-CRITICAL. The OTA writes `size` bytes from 0x4000, so the appended blob eats
 * into the config region that starts right after the stock image at 0xEE08 and runs up to the
 * meter baud record at 0xF4C8 (~1095 bytes of foreign data, surveyed live, NOT recoverable from
 * a canary). Keeping every historical trampoline dragged all their C functions in and grew the
 * blob version by version: v112 ended at 0xF3AD and booted, v113 at 0xF409 and did not.
 * KEEP only the entry that is actually spliced. — places the read-session state probe in the free flash right after
 * reader_stock_v57.bin (44552 B @ base 0x4000 -> next free byte 0xEE08), exactly like
 * link.ld does for the production hooks. Same region, same proven-executable address.
 */
MEMORY
{
  HOOKS (rx) : ORIGIN = 0xEE08, LENGTH = 0x1000
}

ENTRY(entry_iec_swap)

SECTIONS
{
  .text : {
    KEEP(*(.text.entry_iec_swap))
    KEEP(*(.text.entry_probe_nodata))
    *(.text*)
    *(.rodata*)
  } > HOOKS

  /DISCARD/ : {
    *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.ARM.attributes*)
  }
}
