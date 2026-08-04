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

// Paint from here upward; see the v103 note below -- the RAM stub lives under this line.
#define PAINT_LO         0x20001060u

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
// TWO SAU pairs exist and we must know which one is the meter's optical link.
//   group A: config base 0x40041120, prescaler @+6; SDR pair 0x40041310 / 0x40041312.
//            The TX primitive @0x5AD0 writes 0x40041310 and the RX handler @0x63C0 reads
//            0x40041312 -- so this was assumed to be the optical link.
//   group B: config base 0x40041560, prescaler @+6; SDR pair 0x40041748 / 0x4004174A.
//            0xDC60 writes SDR=0xCE00 (divisor 103) here directly.
// At the MEASURED f_CLK of 64 MHz, divisor 103 is 9600 at prescaler 5 and 1200 at prescaler 8,
// while group A's live prescaler of 2 means 115200. Reading B's prescaler decides which pair
// actually carries the 9600 SML link -- and therefore which one a KMP retune must target.
#define SAU_A_SMR        ((volatile u16 *)0x40041126u)  // low nibble = clock prescaler
#define SAU_A_SDR        ((volatile u16 *)0x40041310u)  // divisor in bits [15:9]
#define SAU_A_SDR1       ((volatile u16 *)0x40041312u)
#define SAU_B_SMR        ((volatile u16 *)0x40041566u)
#define SAU_B_SDR        ((volatile u16 *)0x40041748u)
#define SAU_B_SDR1       ((volatile u16 *)0x4004174Au)

// ---- v102: the METER UART's config record, and the region it lives in ------------------------
// The read session does `ldr r0,=0xF4C8 ; ldmia r0!,{r0,r1}` at 0x59F2 and hands those 8 bytes to
// the group-B (meter) setup at 0x59FC. So the meter's baud is a DATA RECORD, layout
// {u32 baud @+0, u8, u8, u8 framing @+6, u8} -- the same shape as the 0xECC8 profile records.
//
// 0xF4C8 is PAST the end of the app image (0xEE08), i.e. in a persistent config area that the
// OTA payload does not cover. That is both why the 0xECC8 table has no xrefs (it is a template)
// and why no baud constant appears anywhere in code.
//
// Before considering any write there we must know what ELSE is in 0xEE08..0xF4C8. If it is all
// erased (0xFF) then extending the OTA image to reach the record is safe; if it holds data, it
// may be pairing/calibration/identity and overwriting it would not be canary-recoverable.
// ---- v103: RAM-resident RX collector -------------------------------------------------------
// The group-B RX callback is invoked from the ISR *during the optical read session* -- the phase
// where branching into the appended blob at 0xEE08 faults (v92..v98). A flash-resident collector
// would reproduce that crash, so the collector must live in RAM. The vendor's own handler at
// 0x635E already does `blx` through this pointer in that phase, so RAM execution there is proven
// by construction.
//
// Hand-assembled Thumb-1, 24 bytes, PC-relative literals so it runs at any 4-aligned address:
//   push {r4,lr}; ldr r4,[pc,#12]; ldr r1,[r4]; adds r1,#1; str r1,[r4]
//   ldr r4,[pc,#8]; blx r4; pop {r4,pc}      ; +0x10 = &counter, +0x14 = original callback
// It counts one byte and then CHAINS to the vendor collector, so SML reception is untouched.
#define STUB_ADDR        0x20001020u                     // 24 B, below PAINT_LO
#define STUB_COUNT       ((volatile u32 *)0x20001040u)   // bytes seen by our collector
#define STUB_ORIG        ((volatile u32 *)0x20001044u)   // saved vendor callback
#define STUB_STATE       ((volatile u32 *)0x20001048u)   // 0 none, 1 installed, 2 refused
#define SAU_B_CB         ((volatile u32 *)0x20000074u)   // group-B RX callback ptr (struct+4)

// ---- v104: does the read session re-apply the group-B config every wake? --------------------
// This decides the whole KMP firmware shape. If the config IS re-applied from the 0xF4C8 record
// each wake, a register-level retune survives only one cycle and the entire
// retune -> TX -> reply -> restore transaction must fit inside a single window.
//
// The test is ASYMMETRIC and that is unavoidable:
//   - reverted to 5  -> a normal report carries the answer (config IS re-applied)
//   - stayed at 8    -> CANNOT report. 1200 baud breaks SML decode, sub_77B4 stops being called,
//                       the hook never runs again, and the prescaler cannot be restored.
//                       Silence IS the answer, and recovery is a canary reflash.
// So it arms exactly once, late (after two full rotations have re-confirmed the baseline in this
// build), and the very first thing the next invocation does is read the prescaler and restore 5.
#define PTEST_STATE      ((volatile u32 *)0x2000104Cu)   // 0 idle, 1 armed, 2 done
#define PTEST_RESULT     ((volatile u32 *)0x20001050u)   // prescaler as found on the next call
#define PTEST_CALLS      ((volatile u32 *)0x20001054u)   // monotonic call count (TURN_ADDR wraps at 16)
#define PTEST_ARM_AFTER  32u                             // calls before arming (~2 full rotations)
// NOTE for the real hook later: kmp_hook.c puts KMP_BUF at 0x20001040, which collides with
// STUB_COUNT. Move one of them before the two ever coexist.

#define CFG_REC          ((volatile u32 *)0x0000F4C8u)   // [+0] baud
#define CFG_REC2         ((volatile u32 *)0x0000F4CCu)   // [+4] flags/framing
#define GAP_LO           0x0000EE08u                     // first byte past the app image
#define GAP_HI           0x0000F4C8u                     // the record itself

// v100: the report-builder entry point. canary_probe() below writes 0x20000D68 (= import at +0),
// which the vendor report builder sub_77B4 OVERWRITES on every telegram -- so its sentinel can
// never reach the gateway. This variant is spliced at 0x7800 inside sub_77B4 instead, the path
// hooks.c:132 records as the one this reader actually uses, and RETURNS the diagnostic in r0 so it
// lands in the power field (+8) as the last writer. Values stay < 0x80000000 so the signed power
// field shows them as positive.
u32 canary_probe_report(u32 power_in);

// Install the RAM collector once. Chains rather than replaces, and refuses if the vendor has no
// callback installed (blx 0 would fault).
static void install_rx_stub(void)
{
    if (*STUB_STATE != 0u) return;                 // already decided
    u32 orig = *SAU_B_CB;
    if (orig == 0u || orig == (STUB_ADDR | 1u)) { *STUB_STATE = 2u; return; }

    volatile u16 *c = (volatile u16 *)STUB_ADDR;
    c[0] = 0xB510u;  // push {r4, lr}
    c[1] = 0x4C03u;  // ldr  r4,[pc,#12]  -> &counter
    c[2] = 0x6821u;  // ldr  r1,[r4]
    c[3] = 0x1C49u;  // adds r1,r1,#1
    c[4] = 0x6021u;  // str  r1,[r4]
    c[5] = 0x4C02u;  // ldr  r4,[pc,#8]   -> original callback
    c[6] = 0x47A0u;  // blx  r4
    c[7] = 0xBD10u;  // pop  {r4, pc}
    *(volatile u32 *)(STUB_ADDR + 0x10u) = (u32)(unsigned long)STUB_COUNT;
    *(volatile u32 *)(STUB_ADDR + 0x14u) = orig;

    *STUB_COUNT = 0u;
    *STUB_ORIG  = orig;
    *SAU_B_CB   = STUB_ADDR | 1u;                  // thumb bit
    *STUB_STATE = 1u;
}

// Prove the group-B prescaler is writable WITHOUT leaving the port misconfigured: write 8, read
// back, restore 5 immediately. Never leaves 1200 baud active, so SML reception cannot break and we
// cannot lock ourselves out (a broken SML stream means no decode phase, hence no more hook calls,
// hence no way to restore -- recoverable only by reflashing the canary).
static u32 prescaler_write_test(void)
{
    volatile u16 *smr = SAU_B_SMR;
    u16 before = (u16)(*smr & 0x000Fu);
    *smr = (u16)((*smr & ~0x000Fu) | 8u);
    u16 after  = (u16)(*smr & 0x000Fu);
    *smr = (u16)((*smr & ~0x000Fu) | before);      // restore immediately
    u16 back   = (u16)(*smr & 0x000Fu);
    return ((u32)before << 8) | ((u32)after << 4) | back;   // expect 0x585
}

static inline u32 read_sp(void)
{
    u32 sp;
    __asm__ volatile ("mov %0, sp" : "=r"(sp));
    return sp;
}

// Returns the deepest (lowest) address still showing the pattern's absence, i.e. the high-water
// mark. 0 while still painting or if nothing was touched.
static u32 probe_core(void)
{
    // FIRST thing, before the write test or anything else can disturb the register: if the
    // persistence test was armed on the previous call, read what the prescaler is NOW and put it
    // straight back to 5. Reaching this line at all already means SML kept decoding.
    if (*PTEST_STATE == 1u) {
        *PTEST_RESULT = (u32)(*SAU_B_SMR & 0x000Fu);
        *SAU_B_SMR = (u16)((*SAU_B_SMR & ~0x000Fu) | 5u);
        *PTEST_STATE = 2u;
    }

    u32 sp = read_sp();
    u32 hi = sp - SP_MARGIN;
    hi &= ~3u;

    if (hi <= PAINT_LO) return 0;               // nothing sane to paint; bail out quietly

    if (*MAGIC_ADDR != MAGIC_VAL) {
        // ---- first call: paint, and report a recognisable "armed" value ----
        for (u32 a = PAINT_LO; a < hi; a += 4) *(volatile u32 *)a = PATTERN;
        *MAGIC_ADDR = MAGIC_VAL;
        *STUB_STATE = 0u;
        *PTEST_STATE = 0u;
        *PTEST_RESULT = 0u;
        *PTEST_CALLS = 0u;
        install_rx_stub();                      // v103: RAM collector, chained (see above)
        return 0x00C0FFEEu;                     // armed sentinel
    }

    // ---- later calls: find the lowest address that is no longer PATTERN ----
    u32 deepest = 0;
    for (u32 a = PAINT_LO; a < hi; a += 4) {
        if (*(volatile u32 *)a != PATTERN) { deepest = a; break; }
    }

    if (deepest == 0) return 0x00D00D00u;       // nothing below `hi` was ever touched

    // One u32 of report per cycle, so rotate through the things worth knowing. The top nibble is
    // an item tag; the gateway just prints the raw value and you decode by tag.
    u32 turn = (*TURN_ADDR) & 15u;   // 11 used, rest repeat the stack mark
    *TURN_ADDR = turn + 1u;
    *PTEST_CALLS = *PTEST_CALLS + 1u;   // TURN_ADDR wraps at 16, so count calls separately

    // if/else, NOT a switch: GCC emits a Thumb-1 jump table calling __gnu_thumb1_case_uqi,
    // a libgcc helper absent from this freestanding -nostdlib link. -fno-jump-tables in
    // build_probe.sh is what actually holds this; GCC rewrites the chain back into a table
    // without it.
    install_rx_stub();                                                // retry if it was refused

    if (turn == 0) return 0x10000000u | (deepest & 0x00FFFFFFu);      // stack high-water
    if (turn == 1) return 0x20000000u | ((*FCLK_ADDR >> 4) & 0x0FFFFFFFu);  // f_CLK / 16
    if (turn == 2) return 0x30000000u | ((u32)(*SAU_A_SDR) << 8) | ((*SAU_A_SMR) & 0x000Fu);
    if (turn == 3) return 0x50000000u | ((u32)(*SAU_B_SDR) << 8) | ((*SAU_B_SMR) & 0x000Fu);
    if (turn == 4) return 0x70000000u | ((u32)(*SAU_B_SDR1) << 8);    // group-B live RX byte
    if (turn == 5) return 0x80000000u | (*CFG_REC & 0x0FFFFFFFu);     // meter baud @0xF4C8

    // ---- v103 ----
    if (turn == 6) return 0x90000000u | (prescaler_write_test() & 0x0FFFFFFFu);
    if (turn == 7) return 0xA0000000u | (*STUB_COUNT & 0x0FFFFFFFu);  // bytes our collector saw
    if (turn == 8) return 0xB0000000u | (*STUB_ORIG  & 0x0FFFFFFFu);  // the vendor callback we chain
    if (turn == 9) return 0xC0000000u | (*STUB_STATE & 0x0FFFFFFFu);  // 0 none / 1 installed / 2 refused
    if (turn == 10) return 0xD0000000u | (*SAU_B_CB  & 0x0FFFFFFFu);  // what the slot holds NOW
    if (turn == 11) return 0xE0000000u | (*PTEST_STATE & 0x0FFFFFFFu);   // 0 idle 1 armed 2 done
    if (turn == 12) return 0xF0000000u | (*PTEST_RESULT & 0x0FFFFFFFu);  // THE ANSWER: 5 or 8

    // Arm once, late: leave the port at 1200 and let the next wake decide.
    if (turn == 13 && *PTEST_STATE == 0u && *PTEST_CALLS > PTEST_ARM_AFTER) {
        *SAU_B_SMR = (u16)((*SAU_B_SMR & ~0x000Fu) | 8u);
        *PTEST_STATE = 1u;
        return 0x0ABCDEF0u;                                           // "test armed" marker
    }
    return 0x10000000u | (deepest & 0x00FFFFFFu);                     // repeat stack
}

// ---- entry points ----------------------------------------------------------------------------
// v99, decode site 0xC0EA: writes the import field directly. SUPERSEDED -- the vendor's report
// builder sub_77B4 rewrites import (+0) on every telegram, so this sentinel never reached the
// gateway. Kept so the v99 image remains reproducible.
void canary_probe(void) { u32 v = probe_core(); if (v) *REPORT_ADDR = v; }

// v100, report site 0x7800 inside sub_77B4: RETURNS the diagnostic so it becomes the stored power
// value, making us the last writer. power_in is the reader's real power, passed through only when
// the probe has nothing to say.
u32 canary_probe_report(u32 power_in) { u32 v = probe_core(); return v ? v : power_in; }

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
