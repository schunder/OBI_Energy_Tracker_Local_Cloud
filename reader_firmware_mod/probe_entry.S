// probe_entry.S — trampoline for the read-session state probe (stack_canary.c).
//
// Injected at 0xC0EA, the SAME decode-phase slot entry_int24 uses — the site whose
// blob-at-0xEE08 execution is live-verified on real hardware (README "Live-verified", v90).
// That is deliberate: the probe must run in the phase already known to work, so that whatever
// it reports is trustworthy and the flash is as low-risk as a canary.
//
// The 4 bytes replaced at 0xC0EA are `movs r0,#0 ; pop {r3-r7,pc}` — a jump-table default slot,
// not a real call. So this trampoline never returns to the patch site: it does its work, then
// replays those two instructions verbatim, unwinding sub_C0A4's own `push {r3-r7,lr}` prologue
// and returning to sub_C0A4's CALLER with r0 = 0, exactly as the original did.
//
// LR does not need preserving (nothing returns here), and canary_probe is AAPCS so it preserves
// r4-r7. r1-r3 are dead at this point: r3 is restored by the pop, and only r0 is a return value.
.syntax unified
.thumb

.section .text.entry_probe, "ax", %progbits
.global entry_probe
.thumb_func
entry_probe:
    bl   canary_probe   // clobbers LR/r0-r3 — all dead here
    movs r0, #0         // replayed original instruction 1
    pop  {r3-r7, pc}    // replayed original instruction 2 -> returns to sub_C0A4's caller

// --- entry_probe_report --------------------------------------------------
// v100. Replaces the 4-byte `LDR R1,=unk_20000D68 ; ADDS R1,#8` at 0x7800 inside sub_77B4 --
// the report-builder path hooks.c:132 records as the one this reader ACTUALLY uses (an
// unconditional sentinel at the rival 0x75EE site never showed up live). On entry R0 holds the
// power value about to be stored via `BL sub_43A6` right after we return; we substitute the
// probe's diagnostic word, then replay the two instructions we displaced.
//
// This exists because v99 wrote 0x20000D68 directly -- which is import at +0, a field the vendor
// rewrites every telegram, so the sentinel could never survive to the gateway.
//
// LR must be preserved: the replaced code was straight-line, and our own `bl` clobbers LR.
.equ obi_report_struct, 0x20000D68

.section .text.entry_probe_report, "ax", %progbits
.global entry_probe_report
.thumb_func
entry_probe_report:
    push {lr}
    bl   canary_probe_report   // r0 = real power in -> diagnostic (or power) out
    ldr  r1, =obi_report_struct
    adds r1, #8                // replayed: r1 = &power field
    pop  {pc}                  // -> sub_77B4 @0x7804, which stores r0 via sub_43A6
