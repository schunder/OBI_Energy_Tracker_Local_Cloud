# BAT32G135 OBI Reader Firmware Analysis: Wake Cycle, Optical Port Gating & PIN LED Architecture

Technical reverse-engineering documentation for `reader_meter_v57.bin` on the BAT32G135 (Cortex-M0+) OBI/heyOBI meter reader.

---

## 1. Executive Summary

This document details:
1. The **wake → read → report → sleep** execution timeline and watchdog / deadline constraints for custom firmware hooks.
2. The dynamic reload behavior of SAU Group-B baud configurations.
3. The hardware power-gating mechanism on the optical probe head (Port 7).
4. The hardware wiring, driver primitives, and state machines driving the **3rd LED / COB (PIN pulse diode)**.
5. Analysis of why direct inline detours inside the optical read session (`sub_59E8`) fault, and why `0xC0EA` / `0x7800` is the optimal hook site.

---

## 2. Wake → Read → Report → Sleep Cycle Timeline

The main execution loop at `0x69EC` drives the reader state machine in five distinct phases:

```
[ Wake from Low-Power Sleep ]
   │ (RTC / Timer Interrupt → HOCO 64 MHz Oscillator Start)
   ▼
[ Step 1: Optical Read Session @ 0x59E8 (called at 0x616C) ]
   ├─ 0x59F2: Loads 9600 8N1 baud record from Flash 0xF4C8 into SAU group B
   ├─ 0x5C08 (in 0x5BFA): Powers ON optical probe (GPIO Port 7 Pin 2 HIGH via 0x7850)
   ├─ 0x5A08: Opens ~115 ms bounded RX window for SML preamble/telegram
   └─ 0x5B58 (in 0x5B4C): Powers OFF optical probe (GPIO Port 7 Pin 0 LOW via 0x7850)
   ▼
[ Step 2: SML Decode & Field Extraction @ 0x6170 - 0x61A2 ]
   ├─ Destuffs frame & validates CRC
   └─ Calls sub_77B4 / 0x7800 / 0xC0EA (Custom Hook Site) to extract energy fields
   ▼
[ Step 3: Radio Payload Formulation & Transmit @ 0x6A00 - 0x6A10 ]
   ├─ Formats LoRaWAN / FSK payload from RAM fields (0x20000D68)
   └─ Powers ON SX126x/Radio & transmits uplink packet
   ▼
[ Step 4: Downlink / OTA Listen Window @ 0x6A10 - 0x6A28 ]
   └─ Listens for RX1/RX2 downlink or gateway OTA commands
   ▼
[ Step 5: Sleep Power-Down @ 0x6A28 ]
   └─ Disables peripherals & enters low-power STOP/DEEPSLEEP mode
```

### Blocking Allowance & Watchdog Safety

* **Radio Timing Deadlines:** Custom hooks running at `0xC0EA` / `0x7800` (Step 2) execute **before** Step 3 initializes and powers up the radio (`0x6A08`). Holding execution for ~1 second to perform an active poll (such as a 1200-baud KMP transaction) introduces **zero radio timing violations**, as no radio link or CAD/RX window is active yet.
* **Hardware Watchdog Timer (WDT):** The BAT32G135 hardware watchdog is clocked by the low-speed internal oscillator (FIL ~15–32 kHz) with a multi-second timeout (~2–4s). System clock routines re-kick `WDTE` (`0xAC` written to `0x40020421`). A 1-second blocking Thumb execution during decode will **not** trip the watchdog or cause a reset.

---

## 3. SAU Peripheral & Baud Rate Management

### Peripheral Mapping
* **SAU Group A (`0x40041120`):** Configured for **115200 8N1** (Prescaler = 2). Hard-wired to the internal debug / expansion pin header.
* **SAU Group B (`0x40041560`):** Configured for **9600 8N1** (Prescaler = 5). Dedicated to the optical head UART link.

### Per-Cycle Baud Reloading
On **every single wake cycle**, `sub_59E8` reads the 8-byte baud/framing configuration record (`80 25 00 00 | 01 01 00 00`) from Flash address `0xF4C8` into the stack frame and passes it into `sub_5EC0` / `sub_DBA0` to configure SAU Group B.

> **Key Takeaway:** Any dynamic register-level retune of SAU Group B (e.g. changing prescaler `0x40041566` from 5 to 8 for 1200 baud) is naturally transient and will automatically reset to 9600 baud at the start of the next wake cycle.

---

## 4. Optical Head Hardware Mapping (Port 7 Architecture)

All signals for the optical reading head are routed to MCU **Port 7 (`P70`–`P73`)**:

| Pin | Function / Peripheral | Purpose |
|---|---|---|
| **`P70`** | `TxD1` (SAU Group B TX) | Optical Transmitter IR LED |
| **`P71`** | `RxD1` (SAU Group B RX) | Optical Receiver Phototransistor |
| **`P72`** | `GPIO P72` | Optical Head Power Enable (VCC Gate) |
| **`P73`** | `GPIO P73` | **3rd LED / COB (Optical PIN Pulse Diode)** |

### Power-Gating Lifecycle & Hook Requirement

* **Power ON:** `0x5C08` (inside `0x5BFA`) calls `sub_7850(r0=7, r1=2, r2=4)` to set **Port 7 Pin 2 HIGH** at the start of the optical session.
* **Power OFF:** `0x5B58` (inside `0x5B4C`) calls `sub_7850(r0=7, r1=0, r2=4)` to set **Port 7 Pin 0 LOW** at the conclusion of `sub_59E8`.

> **Critical Hook Requirement:** Because custom hooks at `0xC0EA` / `0x7800` run in Step 2 (after `sub_59E8` completes), the optical head power has already been turned OFF by `0x5B4C`. Custom hooks that actively transmit/receive on the optical port **must re-enable Port 7 VCC** (via `sub_7850` or direct GPIO bit set) before communicating, and power it down when finished.

---

## 5. 3rd LED / COB (Optical PIN Pulse Diode)

The 3rd LED on the reader head is an optical pulse diode designed for morsing PIN sequences into smart electricity meters (such as German mME / eHz meters).

### Driver Primitives & Control

* **GPIO Driver Primitive (`sub_7850`):**
  - `R0 = 7` (Port 7), `R1 = 3` (Pin 3)
  - `R2 = 1` → Turn LED **ON** (`P73` HIGH)
  - `R2 = 0` → Turn LED **OFF** (`P73` LOW)
* **Mode Setup (`0x5E58`):** Configures `P73` output mode and links it to hardware timer intervals (`bl 0x7830`).
* **Session Teardown (`0x5E6A`):** Called at `0x5A14` during optical session cleanup to force `P73` LOW (`r0=7, r1=3, r2=0`).

### PIN Pulsing State Machines

The firmware implements two dedicated state machines to pulse the PIN diode at structured timing intervals:
1. **State Machine 1 (`0x6D38`):** Controls flash duration and interval stepping (`0x65` ↔ `0x69`).
2. **State Machine 2 (`0xA330`):** Manages multi-digit PIN sequence timing (`0xB6` ↔ `0xB7`).

---

## 6. Disassembly & Fault Analysis of `sub_59E8` Splicing

Direct inline `BL` detours at `0x5A08` or `0x5A18` inside `sub_59E8` fail because:

1. **State Function Bypassing:**
   - `0x5A08` is `bl 0x5bfa` (Optical RX enable & GPIO power-on).
   - `0x5A18` is `bl 0x5a84` (Optical session cleanup & buffer flush).
   - Replacing `0x5A08` without executing `0x5BFA` inside the trampoline leaves SAU Group B uninitialized. Downstream functions (`0x5B78`, `0x5CC0`) then operate on invalid pointers, triggering hardware faults.
2. **Register/Stack Contracts:** `sub_59E8` relies on strict local stack offsets (`sp`, `sp+4`). Detours that alter register state or stack alignment disrupt vendor control flow.
3. **Recommended Hook Location (`0xC0EA` / `0x7800`):** Executing inside the SML decode phase allows custom code to run after optical session cleanup is complete, with a clean stack frame and full control over timing.
