# 🚰 Water metering: Kamstrup KMP on the OBI reader

Technical companion to [`WOLLMILCHSAU.md`](WOLLMILCHSAU.md). This documents the reverse-engineering and the
firmware hook that make an OBI/heyOBI reader (BAT32G135, Cortex-M0+) actively poll a **Kamstrup Multical 21**
water meter over its optical eye — instead of only passively receiving pushed SML electricity telegrams.

> **Status:** hook written & compiles clean; on-meter validation in progress. Nothing here is flashed blind —
> see *Safe flash procedure* below.

## Why it's feasible

The reader's optical port is a full-duplex UART, and the stock firmware **already transmits** on it (it ships
the IEC 62056-21 `/?!` mode-C request string). So "send a query, read a reply" — exactly what KMP needs — is a
pattern the firmware already performs; we're changing *what* it sends, not teaching it a new trick. And KMP is
**1200 baud 8N1**, whose framing is identical to the reader's SML **9600 baud 8N1** — so only the baud rate
changes.

## Reverse-engineering map

All addresses verified from `reader_firmware_mod/reader_stock_v57.bin` (linear objdump cross-checked against
known sites; load base `0x4000`, so `load = file_offset + 0x4000`).

### Optical UART (Serial Array Unit, RL78-style, ARM-mapped)

| Register / primitive | Address | Role |
|---|---|---|
| Optical SAU bank | `0x40041xxx` | UART to the meter (radio SPI is the separate `0x40040xxx` bank) |
| SDR (baud divisor) | `0x40041310` / `0x40041312` | baud in `[15:9]`; `0xCE00` → divisor 103 → 9600 at `f_MCK ≈ 2 MHz` |
| Prescaler nibble | `0x40041126[3:0]` | `f_MCK = f_CLK / 2^prescaler` |
| **TX byte** | `0x5AD0` | `writeByte(b){ *(u8*)0x40041310 = b; }` |
| **RX byte** | `0x40041312` | read; delivered via a callback (see below) |
| **RX callback slot** | `0x2000009C` | optical RX ISR does `if(cb) cb(rxbyte)` — install our collector here |
| **`sendOptical(buf,len)`** | `0x59A4` | copy ≤128 B into TX buffer + interrupt-paced transmit |
| Baud calculator | `0x95AC` | derives `[prescaler, divisor]` from a target baud |
| Optical UART init | `0x94BC` | configures both TX/RX channels + enables their IRQs |
| Read-session orchestrator | `0x59E8` | wake→setup→read cycle; optical setup at `0x5A04` (`bl 0x5AA4`) |
| Energy import field | `0x20000D68` | read out by the LoRa report — our volume drop-point |
| Meter-profile table | `~0xECC8` | `{u32 baud, …}` records + OBIS strings; ships 300 & 9600 profiles |
| Free flash / safe scratch | `0xEE08` / `0x20001000+` | hook code / hook state |

### KMP protocol (field-validated against a real Multical 21)

- **Query** reg `0x0044` (V1 volume, m³): `80 3F 10 01 00 44 4D C0 0D` — start `0x80`, CRC + stop `0x0D` baked in.
- **Reply** e.g. `40 3F 10 00 44 28 04 43 00 04 CB 13 29 23 0D`
  → unit `0x28` (=40, m³), mantissa-len 4, sign/exp `0x43` (exp −3, positive), mantissa `0x0004CB13` = 314131
  → **314.131 m³**.
- **Wire rules:** start `0x80` req / `0x40` reply; stop `0x0D`; escape `{06,0D,1B,40,80}` → `1B, b^FF`;
  CRC = CCITT poly `0x1021`, init 0, **bitwise**, valid when CRC over the destuffed frame == 0.
- The eye is **magnet-activated** (Hall sensor) — the OBI reader's **integrated magnet** wakes it.

## The hook — [`reader_firmware_mod/kmp_hook.c`](reader_firmware_mod/kmp_hook.c)

Three small functions (393 B compiled, `-Werror` clean, Cortex-M0+/Thumb/freestanding):

- **`kmp_poll()`** — installs `kmp_rx_byte` at the RX callback slot, then `sendOptical()`s the fixed query.
- **`kmp_rx_byte(b)`** — bounded, non-blocking collector: appends to scratch until the `0x0D` stop. **Never waits.**
- **`kmp_decode()`** — destuffs → CRC-checks → parses unit/exp/mantissa → litres → writes `0x20000D68`.

Built with atc1441's C-hook framework (`hooks.c` / `entry.S` / `link.ld` / `splice.py`): compile to raw Thumb,
splice into free flash at `0xEE08`, patch a call site to jump in. The full `build → splice` pipeline is
**byte-verified reproducible** (a fresh splice of the stock hooks reproduces the committed known-good binary
exactly).

## Safe flash procedure (write-minimal-then-verify-live)

Two things are **armed on hardware, not written blind** — this is the discipline that keeps the reader
recoverable:

1. **Baud 9600 → 1200** — data-patch the active profile's baud in the table (`~0xECC8`); the vendor calculator
   derives the prescaler/divisor.
2. **Call-site injection** — an `entry.S` trampoline + `splice.py` entry that calls `kmp_poll()` after optical
   setup and `kmp_decode()` after a **bounded** wait.

Recommended sequence, once an **SWD programmer** is on hand (recovery net):

1. **Canary** — flash the hook *dormant* (code present, injection not armed); confirm the reader still pairs and
   reports normally. Proves the image boots.
2. **Arm** — enable the injection + baud switch; put the reader on the Multical 21 (its integrated magnet wakes
   the eye).
3. **Watch** — the volume in m³ appears on your network server / MQTT broker next to the electricity readings.

**Recovery net:** the bootloader validates OTA images (rejects corrupt); the gateway can re-serve
`reader_stock_v57.bin` by advertising a different version; and BAT32 **SWD is not fused** — so a stuck reader is
recoverable with a programmer. The bounded RX wait means a non-responding meter can't hang the radio/OTA phase.

## Verification checklist

- [ ] Canary image boots; reader pairs + reports electricity normally
- [ ] With injection armed, a KMP query is emitted at 1200 baud (logic-analyzer or the meter answering)
- [ ] A CRC-valid reply is collected and decoded to the correct m³ (matches the meter LCD)
- [ ] Volume reaches the network server / MQTT with the right unit
- [ ] Re-serve-stock recovery confirmed working
