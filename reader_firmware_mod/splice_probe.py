"""splice_probe.py -- build the read-session STATE PROBE image.

Produces build/reader_probe_v99.bin: stock v57 + a 4-byte BL at 0xC0EA into the probe
trampoline appended at 0xEE08, softver bumped to 99.

Why this image is safe to flash
-------------------------------
* It injects at 0xC0EA -- the DECODE-phase slot whose blob-at-0xEE08 execution is
  live-verified on real hardware (see README, the v90 mod). Nothing runs in the optical
  read session, which is the phase every v92..v98 image died in.
* The 4 replaced bytes are the jump-table DEFAULT slot `movs r0,#0 ; pop {r3-r7,pc}`
  ("unhandled type -> return 0"). probe_entry.S replays both instructions verbatim, so
  decode behaviour is identical to stock -- unlike entry_int24, which adds int24 decoding.
* Canary-recoverable exactly like v91: if anything goes wrong, re-serve reader_canary_v91.bin.

What it answers (one value per decode-phase report, rotating -- see stack_canary.c):
  tag 1 -> read-session stack high-water mark
  tag 2 -> f_CLK as the firmware itself holds it in RAM  (24 MHz? 2 MHz?)
  tag 3 -> the LIVE group-A SAU SDR + prescaler = the real optical baud, read not inferred
  tag 4 -> f_CLK again, unshifted, as a cross-check
"""
import re
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "reader_stock_v57.bin"
DST = HERE / "build" / "reader_probe_v118.bin"
BLOB = HERE / "build" / "probe.bin"
SYMS = HERE / "build" / "probe.sym"
BASE = 0x4000

# v100: the REPORT-BUILDER site inside sub_77B4 -- `LDR R1,=unk_20000D68 ; ADDS R1,#8` at 0x7800,
# with the power value in R0 about to be stored by the `BL sub_43A6` right after. hooks.c:132
# records this as the path this reader actually uses (a sentinel at the rival 0x75EE never showed
# up live). We become the LAST writer of the reported value.
#
# v99 used the decode site 0xC0EA and wrote 0x20000D68 directly. That address is import at +0,
# which sub_77B4 rewrites every telegram -- so v99's sentinel was clobbered before transmission
# and could never have been seen. Not a negative result about the hook; a bug in the readout.
# v105: the cmd-35 STATUS packet builder's battery store at 0xCB4A
# (`add r1,sp,#32 ; strb r0,[r1,#29]`). The 0x7800 report site is dead on a meter that pushes no
# SML -- sub_77B4 never runs -- but the status packet is built every wake, so this is the only
# channel that reports on the Kamstrup.
# v106: `bl 0x4FF4` at 0xCB8A inside the cmd-37 ENERGY builder (0xCB70) -- the packet the reader
# sends every wake even with no meter (bridge /api/radio shows c=37 with 0x7FFFFFFF sentinels).
# Its import/export/power come from 0x20000DDC, not the 0x20000D68 that sub_77B4 uses.
# v107: the sentinel load at 0xCBB6 on the cmd-37 builder's NO-DATA path -- the path actually taken
# when no meter is attached. Substituting r5 puts our word into import, export AND power.
HOOKS = [
    # telemetry: the cmd-37 no-data path's sentinel load -> our diagnostic word
    (0xCBB6, bytes([0x30, 0x4D, 0x28, 0x46]), "entry_probe_nodata"),
    # v116: the IEC mode-C request setup inside the state machine at 0xD138.
    #   d1a4: movs r1,#5 ; d1a6: adr r0,0xd2cc ; d1a8: bl 0x9738 (length-counted send)
    # Replacing the two setup instructions hands the vendor OUR 9-byte KMP frame, so the firmware
    # transmits it inside its own read session with the optical head powered by its own 0x5BFA.
    (0xD1A4, bytes([0x05, 0x21, 0x49, 0xA0]), "entry_iec_swap"),
]

# softver: 91 canary, 92..98 the armed attempts, 99 = this probe. Must differ from the
# reader's current version or the gateway treats the OTA as a no-op. 99 = v99 probe, 100 = this.
SOFTVER = 118
SOFTVER_OFFSETS = (0x8B36, 0x8B80)


def bl_encode(src_addr, dst_addr):
    """Thumb-2 BL, identical to splice.py's (kept local so the two tools stay independent)."""
    offset = dst_addr - (src_addr + 4)
    assert offset % 2 == 0
    imm25 = offset // 2
    S = 1 if imm25 < 0 else 0
    if imm25 < 0:
        imm25 &= (1 << 24) - 1
    I1, I2 = (imm25 >> 23) & 1, (imm25 >> 22) & 1
    imm10, imm11 = (imm25 >> 11) & 0x3FF, imm25 & 0x7FF
    hw1 = 0xF000 | (S << 10) | imm10
    hw2 = 0xD000 | ((1 ^ I1 ^ S) << 13) | (1 << 12) | ((1 ^ I2 ^ S) << 11) | imm11
    return struct.pack("<HH", hw1, hw2)


def main():
    data = bytearray(SRC.read_bytes())
    orig_len = len(data)
    tramp_addr = BASE + orig_len
    assert tramp_addr == 0xEE08, f"base firmware changed size; probe_link.ld ORIGIN is stale ({hex(tramp_addr)})"

    syms = {}
    for line in SYMS.read_text().splitlines():
        m = re.match(r"^([0-9a-fA-F]+)\s+\S+\s+(\S+)", line.strip())
        if m:
            syms[m.group(2)] = int(m.group(1), 16) & ~1

    for site, orig, entry in HOOKS:
        tgt = syms[entry]
        off = site - BASE
        got = bytes(data[off:off + 4])
        assert got == orig, f"@{hex(site)}: expected {orig.hex()}, found {got.hex()} -- wrong base file?"
        patch = bl_encode(site, tgt)
        data[off:off + 4] = patch
        print(f"patch @{hex(site)}: BL -> {entry}@{hex(tgt)}  bytes={patch.hex(' ')}")

    for so in SOFTVER_OFFSETS:
        assert data[so] == 0x39 and data[so + 1] == 0x20, f"unexpected softver bytes at {hex(so)}: {data[so:so+2].hex()}"
        data[so] = SOFTVER
    print(f"softver 57 -> {SOFTVER}")

    blob = BLOB.read_bytes()
    end = BASE + len(data) + len(blob)
    # HARD LIMIT: the OTA writes `size` bytes from 0x4000, so the blob overwrites the config area
    # that runs from 0xEE08 up to the meter baud record at 0xF4C8. That data is NOT restorable from
    # a canary. v113 reached 0xF409 and the reader refused to boot it.
    LIMIT = 0xF200
    assert end <= LIMIT, (
        "blob ends at %s, past the safe limit %s -- it would overwrite reader config. "
        "Shrink it (KEEP only the spliced entry in probe_link.ld)." % (hex(end), hex(LIMIT)))
    print("blob ends at %s (limit %s, %d bytes of headroom)" % (hex(end), hex(LIMIT), LIMIT - end))
    data += blob
    DST.write_bytes(data)
    print(f"orig {orig_len}  blob {len(blob)}  total {len(data)}")
    print("written:", DST)
    print()
    print("FLASH IT (canary-recoverable; fresh cell in the reader first):")
    print(f'  curl -F firmware=@{DST.name} ".../api/ota?id=3661fe&size={len(data)}&ver={SOFTVER}"')
    print("RECOVER:  re-serve reader_canary_v91.bin (size 44981, ver 91)")


if __name__ == "__main__":
    main()
