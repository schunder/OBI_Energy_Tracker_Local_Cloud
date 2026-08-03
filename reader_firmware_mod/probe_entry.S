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
