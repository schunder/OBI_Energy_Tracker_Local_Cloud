# Outreach to atc1441 — KMP water-poll injection help

Draft messages asking atc1441 for a hint on the one thing blocking the
"Eierlegende Wollmilchsau" reach goal: making the BAT32G135 OBI reader
actively poll a Kamstrup Multical 21 over KMP. See
`../../../.claude/.../memory/obi-reader-kmp-water-re.md` for the full technical
record. Swap in your name/team at the sign-off before sending.

**How to reach atc1441:** GitHub has no private DMs. Best channels, in order:
1. **X/Twitter [@atc1441](https://twitter.com/atc1441)** — most active, DMs usually open. Send the short version.
2. **Their GitHub profile** — check the profile page for a website/email/Discord link (many devs list contact there); atc1441 also runs a blog (atc1441.com) and a Discord community.
3. A public **GitHub Discussion** on the repo (only if you're OK with it being public — not an Issue).

---

## Short version (X/Twitter DM)

Hi atc1441 — we've built a LoRaWAN fork of your OBI reader work (cheap OBI readers → municipal LoRaWAN metering nodes, live join confirmed). Standing entirely on your hook framework + never-brick OTA (which saved us ~6× today 🙏).

Stretch goal: reflash the BAT32 reader to *actively* poll a Kamstrup water meter over KMP. Our hook compiles + links at 0xEE08, canary boots fine — but the moment we splice the call in (replacing `bl 0x5BFA` @0x5A08 inside the read-session at 0x59E8), it faults on boot & rolls back. We bisected it to death: trivial sentinel body, minimal stack, sub-canary image size, correct BL bytes — still faults. Your own 0xC0EA injection works, so it's *this site* that won't tolerate a splice. Can't observe it — SWD won't attach (pins seem remuxed / debug fused).

Any hint on a safe injection point in the optical read path, or a known "mark-valid/watchdog" checkpoint a hook can trip? Even a one-liner would be gold. Happy to share full notes. Thank you!

---

## Long version (GitHub Discussion / email)

**Subject: LoRaWAN + Kamstrup-water fork on your OBI reader work — stuck on one injection site, any insight?**

Hi atc1441,

First, a huge thank-you. Your OBI/heyOBI reverse-engineering and the C-hook framework (`hooks.c`/`entry.S`/`splice.py`, the free-flash trampoline at `0xEE08`, the negative-power fix) are the entire foundation of what we've built. We're a small German municipal-utility team turning inexpensive OBI readers into LoRaWAN metering nodes.

**What we built on top of your work**
- A fork of the ESP32-C3 bridge that adds an EU868 LoRaWAN OTAA uplink, so the reader→bridge data rides out over LoRaWAN to our gateway. Live join + uplinks confirmed on a Multitech Conduit.
- Stretch goal ("does-everything" node): reflash the BAT32G135 reader itself to **actively poll a Kamstrup Multical 21 over KMP** (1200 8N1, request/response) instead of only receiving pushed SML telegrams, and drop the decoded m³ into the existing reader→bridge report.

**What's working**
- Mapped the optical SAU UART (TX `0x40041310` / RX `0x40041312`, callback-driven RX handler at load `0x63C0`, prescaler at `0x40041120+6`, setup fn `0x94BC`). Reused your `sendOptical @0x59A4`.
- The KMP hook compiles clean (Cortex-M0+/Thumb, `-ffreestanding`) and links into free flash at `0xEE08` via a splice modeled on yours.
- A "canary" (hook present but *not* called) flashes and boots fine over your OTA path — and your never-brick net is superb: a faulting app reboot-loops in the bootloader and we recover by re-serving the known-good image. Proven ~6× today.

**Where we're stuck**
We want to fire the KMP poll once per optical read cycle. The read-session orchestrator is at load `0x59E8` — a straight sequence of `bl`s (`…5AA4` optical-setup, `5BFA`, `5B78`, `5CC0`, …). We inject by replacing `bl 0x5BFA` @`0x5A08` with a `bl` to our trampoline, which calls our hook, then the original `0x5BFA`, then returns.

Every armed image faults on boot and rolls back. We bisected hard to isolate it:
- Reduced the hook to a single `str` of a sentinel + return — still faults.
- Minimized the trampoline (8-byte frame; `sub_5BFA` ignores its incoming r0–r3) — still faults.
- Shrank the whole image *below* our known-good canary size (rules out app-size/tail truncation) — still faults.
- Confirmed the sentinel target is a plain data field, not a pointer; and the patched BL + trampoline bytes decode exactly right.
- Your own injection at `0xC0EA` works, so the blob and the mechanism are sound.

So it looks like *this specific site / the read-session context* can't tolerate a spliced call — which matches your note about "cause not fully understood at some injection points." We can't watch it fault: SWD on the BAT32 won't attach (fails at connect even under-reset with NRST wired — pins seem remuxed early, or debug is fused), so we're flying blind.

**Questions, if you ever have a spare minute**
1. Any injection points in the optical read path you've found safe for active TX — or ones you know to avoid?
2. Is there a watchdog or a "mark-image-valid" checkpoint a hook could miss (run too early / add latency) — i.e. could our "fault" actually be a timeout-rollback rather than a CPU fault?
3. Any trick to bring SWD alive on the BAT32G135 (unlock sequence, option byte, specific tool)?
4. Would you make the reader poll a *different* optical protocol via a call-site splice at all, or via the meter-profile table at `0xECC8`?

Totally understand if you're swamped — even a one-line pointer would be gold. Happy to share our full notes/repo. Thanks again for making all of this possible.

— [your name], [municipality/team]

---

## Follow-up detail (ONLY if atc1441 replies and wants specifics — do NOT send up front)

Extra evidence we gathered so we're not just guessing (we did a full bisect + emulation):

- **We emulated it.** Since SWD won't attach to the BAT32, we ran the reader's Thumb code in a Cortex-M0 emulator (Unicorn). The injected path — trampoline → original `sub_5BFA` → our hook — executes **completely clean**, and the whole read-session tail (`0x5A08`→`0x5A20`) is byte-for-byte identical between the armed image and stock. So the fault is **not** in our code's instruction logic.
- **It's not a code-CRC we're tripping.** The image header block is byte-identical across stock/canary/armed, the canary changes code (softver bytes) + appends a blob and boots, and your own `0xC0EA` patch boots — so no broad integrity check is rejecting a modified image.
- **We bisected the real cause on hardware.** Every armed image fault-loops (downloads, boots, faults before check-in, bootloader rolls back — your never-brick net catches it every time). Isolating one variable at a time:
  - trivial one-instruction sentinel hook instead of the real hook → still faults (not the body)
  - same hook injected at a *later* read-session site (`0x5A18`, far from optical-enable) → still faults (not the site)
  - a **pure pass-through trampoline** at `0xEE08` — no hook body at all, it just routes the original `bl 0x5BFA` through `0xEE08` and back, behaviorally identical to stock → **still faults**
- So it's neither the hook body nor the injection site: **merely branching into the appended blob region at `0xEE08` during the optical read-session (`0x59E8`) faults.** Your hooks at `0xEE08` work because they fire in the **SML-decode phase**; ours fail in the **optical read-session phase**. Our working theory: the reader **power-gates the upper flash bank and/or drops the CPU clock during the low-power 9600-baud optical read**, so a call/branch into `0xEE08` bus-faults (the emulator can't model power/clock/flash state, which is why it looks clean there).

**So the concrete questions become:** does the reader gate the upper flash / drop clock during the optical read-session? If so, how would you run added code during the optical window — a safe injection point in a full-power phase, a way to keep the flash bank alive, or somewhere in low/always-on flash to place a poll stub? Even a nudge on whether that power-gating theory is right would save us a lot of blind probing.
