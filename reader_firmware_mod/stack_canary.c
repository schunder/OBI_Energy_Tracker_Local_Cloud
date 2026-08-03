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
#define TURN_ADDR        ((volatile u32 *)0x20001018u)  // which item to report this cycle

// ---- state the read session leaves behind, worth reading from the safe phase ----------------
// f_CLK is a RUNTIME VARIABLE, not a constant: 0x5AA4 loads it from RAM here and passes it as
// arg0 to the UART setup (0xDCB4 -> 0x94BC). Reporting it settles the core-clock question on
// real silicon instead of by inference.
#define FCLK_ADDR        ((volatile u32 *)0x20000390u)
// Group-A SAU (the pair whose data register the TX primitive @0x5AD0 writes):
#define SAU_A_SMR        ((volatile u16 *)0x40041126u)  // low nibble = clock prescaler
#define SAU_A_SDR        ((volatile u16 *)0x40041310u)  // divisor in bits [15:9]

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

    // One u32 of report per cycle, so rotate through the things worth knowing. The top nibble is
    // an item tag; the gateway just prints the raw value and you decode by tag.
    u32 turn = (*TURN_ADDR) & 3u;
    *TURN_ADDR = turn + 1u;

    switch (turn) {
    case 0:  // stack high-water: 0x1xxxxxxx, low 24 bits = the deepest address touched
        *REPORT_ADDR = 0x10000000u | (deepest & 0x00FFFFFFu);
        break;
    case 1:  // f_CLK as the firmware itself believes it: 0x2 + Hz/16 (fits 28 bits up to 4 GHz)
        *REPORT_ADDR = 0x20000000u | ((*FCLK_ADDR >> 4) & 0x0FFFFFFFu);
        break;
    case 2:  // live group-A SAU: 0x3 | SDR<<8 | prescaler nibble
        *REPORT_ADDR = 0x30000000u | ((u32)(*SAU_A_SDR) << 8) | ((*SAU_A_SMR) & 0x000Fu);
        break;
    default: // raw f_CLK low 28 bits, to cross-check case 1 without the shift
        *REPORT_ADDR = 0x40000000u | (*FCLK_ADDR & 0x0FFFFFFFu);
        break;
    }
}

// ---- how to read the result on the gateway ----------------------------------------------------
//   0x00C0FFEE     -> painted, waiting for a read session to happen
//   0x00D00D00     -> nothing touched the painted region (stack never came near)
//   0x1_0010xx     -> STACK high-water at 0x200010xx
//   0x2_xxxxxxx    -> f_CLK / 16, i.e. multiply by 16 for Hz   (expect 0x016E3600 = 24 MHz)
//   0x3_SSSS_p     -> group-A SAU: SDR = bits[27:8], prescaler = low nibble
//   0x4_xxxxxxx    -> f_CLK raw, low 28 bits (cross-check of tag 2)
//
// WHAT THE ANSWERS DECIDE
//   STACK  > 0x20001100 -> stack never nears the KMP scratch: that hypothesis for the v9x faults
//                          dies and KMP_BUF at 0x20001040 is safe where it is.
//          <= 0x20001080 -> the read session runs deep; KMP_BUF must move and the trampoline's
//                          extra frame is a live suspect again.
//
//   f_CLK  24 MHz -> confirms the read-session UART really is 115200 (24e6 / 208 = 115384, +0.16%),
//                   and the long-held "optical link is 9600" assumption is WRONG. The whole KMP
//                   plan retunes 9600 -> 1200; if the link is 115200 the target is 1200 from
//                   115200, a different prescaler, and every baud calculation must be redone.
//          2 MHz  -> the 9600 reading survives and something else explains the 115200 constant
//                   constructed at 0x5A00.
//
//   SDR/prescaler -> the ground truth. SDR 0xCE00 (divisor 103) with prescaler 0 at 24 MHz is
//                   115200; the same SDR with prescaler 3 is 14400, not 1200. Read it, do not
//                   infer it.
//
// Evidence this probe exists to settle (all static, from reader_stock_v57.bin):
//   0x5A00  movs r0,#0xE1 / lsls r0,#9      -> r0 = 115200, passed into the read session's UART setup
//   0x5AA4  mov r1,r0 ; ldr r0,[0x20000390] -> setup(f_CLK from RAM, baud=115200)
//   0xDCB4  push{r0,r1,...} ; sub sp,#4     -> [sp,#8] is that baud, handed to 0x94BC at 0xDD2C
//   0xDD34  b .                             -> a failed baud calculation hangs the firmware forever
