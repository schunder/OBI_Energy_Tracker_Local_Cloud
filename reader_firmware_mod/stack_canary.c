// stack_canary.c — measure the READ SESSION's stack high-water mark using only DECODE-PHASE code.
//
// WHY
// The stack hypothesis for the v92..v98 faults was addressed by shrinking the trampoline (v95
// held 8 bytes instead of 24) but the stack was never MEASURED. This measures it, with zero code
// running in the read session and zero risk of the fault under investigation:
//
//   1. a hook in the DECODE phase (the phase proven safe by the live-verified v90 mod) paints RAM
//      below the current SP with a known pattern, once,
//   2. the reader then runs normally -- including a full optical read session, which uses the
//      stack as deeply as it ever does,
//   3. on a later decode-phase call the hook scans upward for the first word that is no longer the
//      pattern. That address is the deepest point anything reached.
//   4. the result rides out on the existing reader->gateway LoRa report via the import field.
//
// It also answers a question currently answered by assumption: whether KMP_BUF at 0x20001040 sits
// inside the stack's reach. A 64-byte stack buffer already caused one hardfault here.
//
// BUILD (same flags as the other hooks; see build.sh):
//   arm-none-eabi-gcc -c -mcpu=cortex-m0plus -mthumb -Os -ffreestanding -fno-builtin
//     -Wall -Wextra -Werror -ffunction-sections -fdata-sections stack_canary.c
// Splice canary_probe() in at atc1441's proven decode-phase site (~0xC0EA), exactly as the v90
// mod does. Do NOT splice it into the read session -- that would defeat the entire point.

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

// ---- memory map ------------------------------------------------------------------------------
// Real stack top is 0x200016b0 (SP at reset, read from the image header). The stack grows DOWN
// from there, so "deeper" means a LOWER address.
#define STACK_TOP        0x200016b0u

// Paint from here upward. 0x20001000..0x20001010 is atc1441's hook state (PREV_MAGIC / PREV_IMP /
// PREV_EXP / STATE_DIR / STATE_AGE) -- must not be clobbered, so start above it.
#define PAINT_LO         0x20001020u

// Leave this much headroom below our own SP unpainted: we must not paint the frame we are
// standing in, nor the few words the return path will touch.
#define SP_MARGIN        64u

#define PATTERN          0xA5A5A5A5u
#define MAGIC_ADDR       ((volatile u32 *)0x2000101Cu)  // "already painted" flag (below PAINT_LO)
#define MAGIC_VAL        0xCA9A5EEDu
#define REPORT_ADDR      ((volatile u32 *)0x20000D68u)  // import field -> rides the LoRa report

static inline u32 read_sp(void)
{
    u32 sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    return sp;
}

// Returns the deepest (lowest) address still showing the pattern's absence, i.e. the high-water
// mark. 0 while still painting or if nothing was touched.
void canary_probe(void)
{
    u32 sp = read_sp();
    u32 hi = sp - SP_MARGIN;
    hi &= ~3u;

    if (hi <= PAINT_LO) return;                 // nothing sane to paint; bail out quietly

    if (*MAGIC_ADDR != MAGIC_VAL) {
        // ---- first call: paint, and report a recognisable "armed" value ----
        for (u32 a = PAINT_LO; a < hi; a += 4) *(volatile u32 *)a = PATTERN;
        *MAGIC_ADDR = MAGIC_VAL;
        *REPORT_ADDR = 0x00C0FFEEu;             // armed sentinel
        return;
    }

    // ---- later calls: find the lowest address that is no longer PATTERN ----
    u32 deepest = 0;
    for (u32 a = PAINT_LO; a < hi; a += 4) {
        if (*(volatile u32 *)a != PATTERN) { deepest = a; break; }
    }

    if (deepest == 0) {
        *REPORT_ADDR = 0x00D00D00u;             // nothing below `hi` was ever touched
        return;
    }

    // Report the high-water ADDRESS itself (easiest to read on the gateway) plus, in the top
    // byte, how many bytes of headroom remain above PAINT_LO. Both are useful:
    //   value & 0x00FFFFFF = deepest address, low 24 bits (0x20000000 base is implicit)
    //   value >> 24        = (deepest - PAINT_LO) / 16, saturated at 255 -- the margin
    u32 margin = (deepest - PAINT_LO) >> 4;
    if (margin > 255u) margin = 255u;
    *REPORT_ADDR = ((margin & 0xFFu) << 24) | (deepest & 0x00FFFFFFu);
}

// ---- how to read the result on the gateway ----------------------------------------------------
//   0x00C0FFEE            -> painted, waiting for a read session to happen
//   0x00D00D00            -> nothing touched the painted region at all (stack never came near)
//   0xMM0010xx            -> high-water at 0x200010xx; MM = free 16-byte units above 0x20001020
//
// INTERPRETATION
//   deepest > 0x20001100  -> the stack never comes near the KMP scratch; the stack hypothesis for
//                            the v9x faults is dead, and KMP_BUF at 0x20001040 is safe where it is.
//   deepest <= 0x20001080 -> the read session runs deep. KMP_BUF at 0x20001040 is in the blast
//                            radius and must move, and the v9x trampoline's extra frame becomes a
//                            live suspect again.
