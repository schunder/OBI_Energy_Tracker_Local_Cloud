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

// --- entry_probe_batt ----------------------------------------------------
// v105. The Kamstrup pushes no SML, so sub_77B4 never runs and the 0x7800 site is dead. But the
// cmd-35 STATUS packet is still built every wake (that is why softver and battery keep arriving),
// so we hook its battery store instead and use the battery byte as an 8-bit report channel.
//
// Replaces the 4 bytes at 0xCB4A: `add r1,sp,#32 ; strb r0,[r1,#29]`, with R0 holding the battery
// value (already divided by 20) about to be stored into the packet buffer. We substitute our
// diagnostic byte and replay the store.
//
// sp compensation: `push {lr}` moves sp down 4, so the replayed `add r1,sp,#32` becomes #36.
.section .text.entry_probe_batt, "ax", %progbits
.global entry_probe_batt
.thumb_func
entry_probe_batt:
    push {lr}
    bl   canary_probe_batt   // r0 = real battery byte in -> diagnostic byte out
    add  r1, sp, #36         // replayed (was sp,#32; +4 for the pushed lr)
    strb r0, [r1, #29]       // replayed store into the cmd-35 buffer
    pop  {pc}

// --- entry_probe_energy --------------------------------------------------
// v106. Replaces the 4-byte `bl 0x4FF4` (battery read) at 0xCB8A, inside the cmd-37 ENERGY packet
// builder at 0xCB70 -- the packet the reader sends EVERY wake even with no meter data (confirmed
// on the bridge's /api/radio: c=37, imp/exp/pow = 0x7FFFFFFF sentinels).
//
// That builder sources import/export/power from the struct at 0x20000DDC (literal @0xCC7C), NOT
// the 0x20000D68 that sub_77B4 and atc1441's hooks use. Writing 0x20000D68 is why the v104
// diagnostics disappeared the moment the reader left the SML meter.
//
// 0x4FF4 takes no arguments and returns the raw battery in r0, so running our probe first is safe:
// it may clobber r0-r3 freely, and 0x4FF4's return value flows on untouched.
.equ vendor_batt_read, 0x4ff4

.section .text.entry_probe_energy, "ax", %progbits
.global entry_probe_energy
.thumb_func
entry_probe_energy:
    push {r4, lr}
    bl   canary_probe_energy      // stuff diagnostics into 0x20000DDC[0/4/8]
    ldr  r4, =(vendor_batt_read | 1)
    blx  r4                       // replayed original call; r0 = battery raw
    pop  {r4, pc}

// --- entry_probe_nodata --------------------------------------------------
// v107. The cmd-37 builder has TWO paths, selected by a flag byte at [sp+32+14]:
//   flag==1 -> 0xCBE4, reads real values from the struct at 0x20000DDC
//   else    -> 0xCBB4, stuffs the CONSTANT 0x7FFFFFFF (literal @0xCC78) into import, export and
//              power via three sub_43A6 calls
// With no meter the reader always takes the second path, which is why writing 0x20000DDC (v106)
// changed nothing. So hook the sentinel load itself.
//
// Replaces `ldr r5,[pc,#192] ; mov r0,r5` at 0xCBB6. r5 becomes our diagnostic instead of
// 0x7FFFFFFF, and since the path reuses r5 for all three fields, import/export/power all carry it.
//
// r1 MUST be preserved: it was loaded at 0xCBB4 (the import destination pointer) and is consumed by
// the `bl 0x43a6` at 0xCBBA, but a C call is free to clobber it. r2/r3 are dead across this window.
.section .text.entry_probe_nodata, "ax", %progbits
.global entry_probe_nodata
.thumb_func
entry_probe_nodata:
    push {r1, lr}
    bl   canary_probe_nodata   // -> r0 = the value to send instead of 0x7FFFFFFF
    mov  r5, r0                // replayed (was: r5 = literal 0x7FFFFFFF)
    mov  r0, r5                // replayed
    pop  {r1, pc}

// --- entry_iec_swap ------------------------------------------------------
// v116. THE VENDOR TRANSMITS FOR US.
//
// 0xD138 is an IEC 62056-21 mode-C state machine: it sends "/?!\r\n" and then ACKs with "\x06 000".
// Our RAM collector captured 2F 3F 21 off the eye glass, so this path runs on the Kamstrup -- the
// reader is already doing active optical request/response, inside the read session, with the head
// powered by its own 0x5BFA. The meter just does not speak IEC.
//
// The send is LENGTH-COUNTED, not NUL-terminated:
//     d1a4: movs r1,#5        <- length
//     d1a6: adr  r0,0xd2cc    <- buffer
//     d1a8: bl   0x9738       <- send(buf,len)
// so a frame containing 0x00 is fine, and we only need to supply different r0/r1. We replace the
// two setup instructions (4 bytes) and hand the vendor OUR frame. The buffer lives in our blob, so
// the 8-byte slack at 0xD2CC and the lack of a free ADR-reachable slot stop mattering.
//
// Also forces group-B to 1200 baud first: prescaler nibble at 0x40041566, 5 -> 8 (divisor 103
// unchanged => 64e6/2^8/208 = 1201.9 baud). Measured writable in v103, and the next wake restores
// 9600 from the 0xF4C8 record by itself (v104), so there is nothing to undo.
//
// Returns with r0/r1 set exactly as the replaced instructions would have, so the vendor's `bl` at
// 0xD1A8 proceeds untouched.
.section .text.entry_iec_swap, "ax", %progbits
.global entry_iec_swap
.thumb_func
entry_iec_swap:
    push {lr}
    bl   kmp_iec_setup         // vendor group-B init at 1200 + snapshots; returns r0 = frame
    movs r1, #9                // replayed length, now 9
    pop  {pc}                  // -> 0xD1A8: bl 0x9738 sends OUR frame at 1200
