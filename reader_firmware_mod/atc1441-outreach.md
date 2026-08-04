# Outreach to atc1441 — one question about the reader's wake/read cycle

Asking atc1441 the single thing we cannot determine ourselves about the BAT32G135 OBI reader.
Full technical record: `../../../.claude/.../memory/obi-reader-kmp-water-re.md`.
Swap in your name/team at the sign-off before sending.

> **REWRITTEN 2026-08-04.** The previous draft asked how to inject into the optical read session at
> `0x59E8`. That question is obsolete — we no longer need to run code there — and worse, it stated
> the optical UART as `0x40041310/12` with `sendOptical @0x59A4`. Live measurement showed that is a
> **different UART**. Sending the old draft would have handed atc1441 wrong facts. It is preserved
> at the bottom, clearly marked, because its final question (why any splice in `0x59E8` faults) is
> still unanswered and worth a bonus ask.

**How to reach atc1441:** GitHub has no private DMs. Best channels, in order:
1. **X/Twitter [@atc1441](https://twitter.com/atc1441)** — most active, DMs usually open. Send the short version.
2. **Their GitHub profile** — website/email/Discord link; atc1441 also runs a blog (atc1441.com) and a Discord.
3. A public **GitHub Discussion** on the repo (only if you're OK with it being public — not an Issue).

---

## Short version (X/Twitter DM)

Hi atc1441 — we've built a LoRaWAN fork of your OBI work (cheap OBI readers → municipal LoRaWAN
metering nodes, live join confirmed), standing entirely on your hook framework and never-brick OTA,
which has saved us ~10× now. 🙏

Stretch goal: make the BAT32 reader *actively* poll a Kamstrup water meter over KMP (1200 8N1
request/response) alongside its normal SML job. We've measured the reader live — hooking your
`0x7800` site and reporting through the power field — and found the meter is on SAU **group B**
(`0x40041560` / `0x40041748`, prescaler `0x40041566` = 5 → 9600 @ f_CLK 64 MHz), while group A at
115200 never moves. The baud isn't even a constant: `0x59F2` reads an 8-byte record from `0xF4C8`
and `0x59FC` feeds it to the group-B setup.

The one thing we can't see from the binary: **the timing of the wake → read → report → sleep
cycle.** Can firmware hold the optical port for ~1 s of request/response without tripping a
watchdog or a report deadline — and is the group-B config re-applied from `0xF4C8` on every wake
(so a register-level retune only lasts one cycle)? Even a one-liner would be gold. Happy to share
everything we've mapped.

---

## Long version (GitHub Discussion / email)

**Subject: KMP water-poll on your OBI reader — one question about the wake/read cycle timing**

Hi atc1441,

First, thank you. Your OBI/heyOBI reverse-engineering and the C-hook framework (`hooks.c` /
`entry.S` / `splice.py`, the free-flash trampoline at `0xEE08`, the negative-power fix) are the
entire foundation of this. We're a small German municipal-utility team turning inexpensive OBI
readers into LoRaWAN metering nodes. Your never-brick OTA path has recovered us from every bad
image, about ten times now.

**What we're doing:** adding a Kamstrup Multical 21 KMP poll (1200 8N1, request/response) to
`reader_meter_v57.bin`, so one reader does SML electricity *and* water, with the decoded m³ riding
out in the existing reader→bridge report.

**What we measured on hardware.** SWD won't attach on this part, so we built a probe: your `0x7800`
site in `sub_77B4`, returning a diagnostic word in R0 so it lands in the reported power field. One
value per report, rotating. Results:

- `f_CLK` = **64 MHz**, read from the RAM word at `0x20000390` (the same word `0x5AA4` and `0x5EC0`
  both load and pass to the UART setups).
- SAU **group B** — cfg `0x40041560`, SDR `0x40041748`/`0x4004174A`, prescaler `0x40041566` — reads
  **5**, i.e. 9600 8N1, and its RX register carries live changing data. **This is the meter link.**
- SAU **group A** — cfg `0x40041120`, SDR `0x40041310`/`0x40041312`, prescaler `0x40041126` — reads
  **2**, i.e. 115200, and its registers never change. **What is group A actually connected to?** We
  had it mapped as "the optical UART" for weeks and it isn't.
- The meter baud is **data, not code**: `0x59F2` does `ldr r0,=0xF4C8 ; ldmia r0!,{r0,r1}` and
  `0x59FC` hands those 8 bytes to the group-B setup at `0xDBA0`, which takes baud from `[r4+0]` and
  a framing selector from `[r4+6]`. Live content is `80 25 00 00 | 01 01 00 00` — 9600, framing 0 —
  byte-identical to the template record at `0xECC8`.

**The plan**, which avoids the read session entirely: run the whole transaction from the decode
phase (your `0xC0EA` / `0x7800` sites, which we've confirmed execute in our own build) — bump the
group-B prescaler 5 → 8 for 1200 baud, clock a 9-byte KMP request out through the byte writer at
`0x5F64`, collect the reply via the callback pointer at `0x20000074`, restore prescaler 5, decode.

**The question — the one thing the binary can't tell us:**

> **What does the wake → read → report → sleep cycle look like in time, and is there a point where
> added firmware can hold the optical port for roughly one second of request/response without being
> killed by a watchdog, a report deadline, or the OTA-listen window?**

Concretely:

1. Is the group-B UART config re-applied from `0xF4C8` on **every** wake? If so a register-level
   retune only survives one cycle, and the whole KMP transaction has to fit inside one window.
2. Is there a watchdog or a deadline between the SML decode and the LoRa report that a bounded
   ~1 s blocking wait would trip? (A Multical 21 needs a few hundred ms to answer, and 15 bytes at
   1200 baud is another ~125 ms.)
3. Is the optical receiver powered continuously while awake, or gated to a short window around the
   expected telegram?
4. Any idea what group A (115200) is wired to?

**Bonus, only if you happen to know offhand:** any spliced `BL` inside the read session at `0x59E8`
— we tried `0x5A08` and `0x5A18`, including a pure pass-through trampoline that changes nothing but
timing — makes the image fault before it checks in, while the same blob at `0xEE08` called from
`0xC0EA` runs fine. Is something in that phase power-gating the upper flash bank or dropping the
clock? We spent a long time on this before routing around it; a one-line answer would close it.

Totally understand if you're swamped. Even a partial answer to (1)–(3) decides our design. Happy to
share the full notes, the probe tooling, and the corrected register map.

— [your name], [municipality/team]

---
---

## SUPERSEDED (2026-07-31 draft) — do not send

Kept for the record. Its factual claims about the optical UART are **wrong** — every address it
cites belongs to SAU group A, which live measurement showed is *not* the meter link (it runs at
115200 and never carries data). The design question it asks was also made moot: we no longer need to
execute anything in the read session.

The one part still live is its final question, folded into the "Bonus" paragraph above: **branching
into the appended blob at `0xEE08` during the optical read-session (`0x59E8`) faults, while the same
blob called from the SML-decode phase (`0xC0EA`) works.** Bisected on hardware — trivial sentinel
body, minimal stack frame, image smaller than a booting canary, a later injection site, and finally
a pure pass-through detour — all fault identically. Emulated clean in Unicorn, so it is not
instruction logic. Working theory at the time: the reader power-gates the upper flash bank and/or
drops the CPU clock during the low-power optical read.

Original wrong-map claims, for the avoidance of doubt: *"optical SAU UART (TX `0x40041310` / RX
`0x40041312`, callback-driven RX handler at `0x63C0`, prescaler at `0x40041120+6`, setup fn
`0x94BC`), reused your `sendOptical @0x59A4`."* The corrected map is in the long version above.

Also superseded from that draft: a request for help bringing SWD alive on the BAT32G135. Still
true that it won't attach, but it no longer blocks anything — the report-field probe replaced it as
our observation channel, and it has answered every question we've put to it.
