# OBI Energy Tracker → LoRaWAN bridge

Add a **standard LoRaWAN OTAA (EU868) uplink** to atc1441's open OBI/heyOBI gateway firmware, so the
cheap OBI electricity readers become nodes of any LoRaWAN network server (ChirpStack, TTN, …) — the
gateway keeps doing everything it already does (pair readers, decode energy on-device, web dashboard,
MQTT), and additionally forwards each reader's energy over LoRaWAN.

## Credit & inspiration

This is a **fork** of [**atc1441/OBI_Energy_Tracker_Local_Cloud**](https://github.com/atc1441/OBI_Energy_Tracker_Local_Cloud)'s
`open_obi_energy_meter/` firmware. **All of the hard work — and all credit for it — is Aaron
Christophel's (atc1441):** the complete reverse-engineering of the OBI/heyOBI system (the proprietary
868 MHz LoRa protocol, ECDH key exchange, TEA-ECB energy decryption, beacon timing) and the entire
base ESP32-C3 gateway firmware (reader pairing, on-device decode, web dashboard, MQTT/Home-Assistant,
reader-OTA, self-update). Without that this fork would not exist. Please watch/read his work:

- Original repo: <https://github.com/atc1441/OBI_Energy_Tracker_Local_Cloud> (this fork's parent)
- Explainer videos: <https://www.youtube.com/watch?v=2jMEaRuSJ18> (and the series linked from his repo)
- Hackaday writeup: <https://hackaday.com/2026/07/07/reverse-engineering-and-self-hosting-the-obi-smart-energy-tracker/>

The LoRaWAN uplink is built on **[RadioLib](https://github.com/jgromes/RadioLib)** by Jan Gromeš
(`jgromes/RadioLib @ 7.7.1`). Upstream `LICENSE` and `DISCLAIMER.md` are preserved unchanged; this
fork adds code, it does not relicense atc1441's work, and it ships **no** vendor firmware binaries.

> **Fork base:** this branch is built and verified against upstream commit `15a32ec` (the v1.0.22
> era). atc1441's `main` has since advanced (incl. a runtime SF7/SF9 range feature that touches the
> radio-config code this fork also uses). Rebasing onto the latest upstream is planned, but it needs
> to be re-validated on hardware first — so the fork is intentionally pinned to the base it was
> proven against rather than shipping an untested merge.

## What this fork adds

Our delta is a **new subsystem** that takes the plaintext energy struct the base firmware already
decodes per reader and uplinks it over standard **LoRaWAN OTAA (EU868)** to a ChirpStack network
server, so an OBI/heyOBI electricity reader becomes a first-class node of an existing LoRaWAN
metering fleet. Full rationale, protocol facts, and work-package plan:
`../OBI_LoRaWAN_Bridge_Implementation_Plan.md`.

Our delta is a **new subsystem** that takes the plaintext energy struct the base firmware already
decodes per reader and uplinks it over standard **LoRaWAN OTAA (EU868)** to the municipal
ChirpStack, so an OBI/heyOBI electricity reader becomes a first-class node of the existing
LoRaWAN metering fleet. Full rationale, protocol facts, and work-package plan:
`../OBI_LoRaWAN_Bridge_Implementation_Plan.md`.

## Real-hardware status (first physical unit, 2026-07-25)

A real stock bridge (softver `1.0.1(58)`, hw `6`) was brought up on UART0 and put through the
self-hosting flow end to end:

- **TEA key read via UART cmd 49** (`03-reverse-engineering/uart-config-protocol.md`): returns the
  unit's UUID, BLE-ID (`OBI-XXXXXX`) and 16-byte TEA key. (Our real unit's values are kept out of
  this public repo — the TEA key is the device's BLE control-channel secret. Read your own with the
  cmd-49 procedure.)
- **Hardware gotcha**: the bridge went into a `BROWNOUT_RST` boot loop on a bare USB-TTL adapter.
  Root cause was a bench-power issue (not wiring) — resolved once powered from a lab supply.
- **Vendor-cloud stock-firmware backup**: attempted via `POST /device-provisionings` +
  `obi_ota_download.py`, but that endpoint only issues a short-lived (~7 min) AWS-IoT **claim**
  cert, scoped to fleet-provisioning topics only — not enough to pull OTA data. Getting a real
  backup would need replaying the claim→permanent-cert exchange against the real OBI cloud (new
  reverse-engineering work, not attempted). `tools/fetch_device_provisioning.py` (new, added here)
  gets you the claim cert if you want to pick this up later.
- **Self-hosting flow — done, verified working**: `gen_certs.py` → `mqtts_server.py` (our own
  broker) → `ble_provision.py --unbind --no-pair-sensor --ssid ... --password ...`. The bridge
  unbound from the vendor cloud, took our CA/cert config over BLE, joined WiFi, and connected to
  our broker (`CONNECT` + fleet-provisioning + `registered thing` all logged). **No reader was
  attached to pair this session** — do `ble_provision.py` again (drop `--no-pair-sensor`) once one
  is available; BLE closes once the device goes operational, so re-open it with a 5 s button hold.
- **OTA push — SUCCESS, confirmed 2026-07-26**: `mqtts_server.py --ota-firmware` pushed
  `tools/Precompiled_Open_Obi_Energy_Tracker_replacement_firmware_1.0.22.bin` (the repo's
  precompiled **base** firmware, not our LoRaWAN fork — see why below). The bridge rebooted into
  it and came up clean: `GET /api/status` reports
  `{"fw":{"version":"1.0.22","build":22,"target":"obi_gateway_c3","git":"d9de1c8-dirty"}}`, joined
  WiFi (`192.168.1.138`), radio initialized (869.5 MHz/BW500/SF7), dashboard live at
  `http://192.168.1.138/`. **Correction to a wrong assumption from the first session**: the
  AWS-IoT-style `04-connect-your-own-cloud` broker flow (`gen_certs.py`/`mqtts_server.py`/
  `ble_provision.py`) only applies to the **stock vendor firmware** — once the OPEN replacement
  firmware is running, it has its own completely separate, much simpler local MQTT setting
  (`mqtt.enabled` in `/api/status`, off by default) and never talks to that AWS-IoT broker at all.
  Don't expect a `CONNECT` in the `mqtts_server.py` log after this point — that's expected, not a
  failure.
- **WP0 acceptance — met, 2026-07-26**: reader physically clipped onto the home electricity meter,
  `POST /api/pairall` opened the 3-min auto-accept window, reader bound within seconds. `GET
  /api/readers` confirms live decoded telemetry: `import`/`export` cumulative Wh, `power` (signed
  int32 — small negative values like -5 to -26 W are a real near-balanced net-export reading, not a
  bug), `battery_mV`, `rssi`, refreshing every ~10-20 s. This is the legacy "v32" reader generation
  (`softver:32`, `legacy:true`). **Still open**: the 12 h soak (WP0's last bullet).
- **Our LoRaWAN fork could not be pushed today** — `pio run -e obi_gateway_c3` doesn't produce a
  working `firmware.bin` in this dev environment yet (see "Build status" below): real mbedtls
  linker errors (`mbedtls_ssl_init` etc. undefined) from this specific "pioarduino" registry
  framework build's oddly split `libmbedtls.a` / `libmbedtls_2.a` packaging. Two other framework
  gaps in the same area were found and fixed along the way (see `lib/NetworkClientSecure/` and the
  `platformio.ini` LTO note) — this last one is unresolved. Not a problem with our own LoRaWAN code
  (`main.cpp`, `obi_to_lorawan.cpp`, etc. all compile clean) — just this environment's toolchain
  packaging. Next attempt: try a mainline PlatformIO/Arduino-ESP32 install instead of this
  "pioarduino" community registry, or dig into why `-lmbedtls_2`/`-lmbedcrypto`/`-lmbedx509` aren't
  satisfying symbols `ssl_client.cpp` needs despite being on the link line.

## Radio mode: SHARED (single radio), not DUAL

The plan document defaults to a dual-radio design (Option A) for robustness, but this fork
implements **Option B — SHARED, single SX1262** first, per an explicit decision: the hardware is
already on hand and cheap, and OBI readers only report every `upload_interval` seconds (default
25 s here, up to 300 s stock) — comfortably enough slack to borrow the radio for one LoRaWAN
transaction (~2.1 s: TX + RX1 + RX2) without readers losing their beacon sync (they tolerate
several missed 1 Hz beacons via their own retry/backoff). DUAL is not implemented; if SHARED
proves too fragile in the field (WP5 soak test), see the plan's Option A for the fallback design
— it is a hardware change (second SX1262 module + a build-flag split), not scoped here.

## What's new (WP1–WP4 code)

| File | Purpose |
|---|---|
| `include/obi_radio_params.h` | Single source of truth for the OBI PHY constants + shared `radio`/`onDio1` externs, so the boot-time init and the post-uplink restore can't drift apart |
| `include/board_config.h` (edited) | **Bug fix**, see below |
| `src/main.cpp` (edited) | RF-switch fix; hooks `obi_lorawan_setup()`/`obi_lorawan_tick()` into `setup()`/`loraTask` |
| `include/lorawan_config.h` | Region/FPort/timing tunables |
| `include/lorawan_secrets.h.example` | OTAA credential template (copy to `lorawan_secrets.h`, git-ignored) |
| `include/obi_deveui.h`, `src/obi_deveui.cpp` | Fleet DevEUI convention (`FE-07-01-...`) |
| `include/lw_persist.h`, `src/lw_persist.cpp` | NVS nonce/session persistence (own namespace `obilw`) |
| `include/lorawan_uplink.h`, `src/lorawan_uplink.cpp` | RadioLib `LoRaWANNode` OTAA wrapper, bound to the **same** `radio` instance the OBI master uses |
| `include/payload_codec.h`, `src/payload_codec.cpp` | WP3 wire frame (schema v1, 15 B/reader) |
| `include/obi_to_lorawan.h`, `src/obi_to_lorawan.cpp` | The SHARED-mode scheduler: runs right after each beacon TX, borrows the radio for a join/uplink attempt, restores the OBI PHY afterward |
| `chirpstack/codec.js`, `chirpstack/device-profile.md` | WP4 — ChirpStack-side decoder + registration steps |

## Bug fix found while wiring this up: XIAO+Wio RF switch

`board_config.h`'s `OBI_BOARD_XIAO_ESP32S3` preset (upstream) set `LORA_DIO2_RFSW = true`, but
the Wio-SX1262 B2B module's antenna switch is wired to **GPIO38**, not the SX1262's DIO2 — this
is a hard-won lesson from `LoRaWANsensors/SeeedXIAOLoRaWAN` (proven on RadioLib 7.0.2; DIO2 isn't
connected to the switch on this module). Left as-is, this preset would have produced a "deaf RX"
gateway: TX might work by luck, but nothing is ever received — no reader announce, no LoRaWAN
JoinAccept — the exact failure mode already hit once before on this hardware family (see
`memory/xiao-wio-hat-flash-workflow.md`). Fixed: `main.cpp`'s radio init now prefers
`setRfSwitchPins(LORA_RXEN_PIN, NC)` whenever the board defines `LORA_RXEN_PIN`, falling back to
`setDio2AsRfSwitch()` only for boards without one (Heltec/T-Beam presets keep working unchanged).

## RadioLib pinned to 7.0.2

Upstream pins `^7.1.0`. Repinned to the exact `7.0.2` the rest of the municipal fleet runs —
`7.7.1` has a known RX regression (`jgromes/RadioLib#1806`) already documented in
`memory/radiolib-771-rx-regression.md`. Re-evaluate only after that's confirmed fixed **and**
re-tested on our hardware.

## Build status (verified in this environment)

`pio run -e xiao_esp32s3` was actually run end-to-end (RadioLib 7.0.2, framework-arduinoespressif32
3.3.8, toolchain-xtensa-esp-elf 15.2.0) after copying `lorawan_secrets.h.example` to a scratch
`lorawan_secrets.h` for the test. **Every new/edited file compiles clean**: `main.cpp`,
`obi_radio_params.h`, `lorawan_config.h`, `obi_deveui.cpp`, `lw_persist.cpp`,
`lorawan_uplink.cpp`, `payload_codec.cpp`, `obi_to_lorawan.cpp` — no errors, no warnings. The
RadioLib `LoRaWANNode` API (`beginOTAA`, `activateOTAA`, `setBufferNonces`/`setBufferSession`,
`sendReceive`, the `RADIOLIB_LORAWAN_*` constants) matches what `lorawan_uplink.cpp` assumes, so
no API drift from the fleet's proven `LoRaWANManager.cpp` pattern.

The **only** remaining build failure is unrelated to any of this: `src/gateway_web.cpp` (pure
upstream atc1441 code, not touched here) `#include <WiFiClientSecure.h>` for its GitHub-OTA-check
and MQTTS paths, and the `framework-arduinoespressif32@3.3.8+sha.f2a3fa2b` package this
PlatformIO/pioarduino registry resolved (a community/tasmota build) doesn't ship that header at
all — a framework-package-selection gap in this dev environment, not a code defect. It may not
reproduce on your machine (different PlatformIO/registry state); if it does, look at pinning
`platform_packages` to a mainline `espressif/arduino-esp32` release instead. Note also: getting a
working PlatformIO invocation here required repointing `~/.platformio/penv` at a Python 3.11
interpreter (this environment's default is much newer, which esptool's helper install script and
`littlefs-python`'s compiled wheel both choke on) — likely irrelevant on a normal dev machine, but
if you hit `idf_tools.py`/`littlefs`/`penv` errors, that's the same failure class.

### `obi_gateway_c3` (the actual stock-hardware target) — NOW BUILDS (2026-07-27)

`pio run -e obi_gateway_c3` produces a working `firmware.bin` (1,026,672 bytes; 52.2% of the
1.9 MB OTA slot, 29.3% RAM). The mbedtls link wall (below) was resolved by **compiling out TLS
entirely** via `-D OBI_NO_TLS` (in the env's `build_flags`): this framework build ships mbedtls
*crypto* but not the standalone `mbedtls_ssl_*` TLS handshake layer, and those symbols aren't in
any archive on disk — confirmed via `nm`/`objdump`, so it's genuinely absent, not a link-order
bug. The LoRaWAN-bridge fork needs no TLS: the only `WiFiClientSecure` users are two optional
convenience features — **MQTTS** (default off; plain MQTT on 1883 still works) and the **GitHub
self-updater** (HTTPS). Both are now `#ifndef OBI_NO_TLS`-guarded in `gateway_web.cpp` (5 surgical
sites: the include, `netTls`, `applyMqttClient`, `githubLatest`, `ghOtaTask`). The vendored
`lib/NetworkClientSecure/` is left in place but simply isn't compiled under `OBI_NO_TLS` (nothing
includes it) — useful if a future *mainline* Arduino-ESP32 framework (which does ship the SSL
layer) is used instead, where TLS could be re-enabled by dropping the flag.

**Historical (the wall this replaced):** before `OBI_NO_TLS`, the link failed with core mbedtls
symbols (`mbedtls_ssl_init`, …) undefined. What got fixed along the way and is still relevant:

- **Fixed, committed to this fork**: `lib/NetworkClientSecure/` (vendored from upstream
  espressif/arduino-esp32 @ 3.3.8 — this framework build renamed `WiFiClientSecure` to
  `NetworkClientSecure` and dropped the compat header entirely) with two patches on top: its
  `ssl_client.cpp` unconditionally skipped its ENTIRE implementation behind an
  `#if !defined(MBEDTLS_KEY_EXCHANGE_SOME_PSK_ENABLED)` guard (this framework's mbedtls has PSK
  disabled, which upstream treats as "broken build" and silently compiles to an empty file,
  dropping every TLS symbol at link time) — patched to `#if 0` so the real implementation always
  compiles; the two genuinely-PSK/ALPN-specific mbedtls calls inside are now individually
  `#ifdef`-guarded instead. `platformio.ini`'s `obi_gateway_c3` env also gets `build_unflags =
  -flto` (LTO was dropping real symbols at link time on this toolchain).
- **Fixed, local-machine-only (NOT in this repo, redo if this environment is rebuilt)**: the
  `toolchain-riscv32-esp` package this registry serves is a tiny stub that depends on ESP-IDF's
  `idf_tools.py` to fetch the real ~964 MB compiler, and that script refuses to run under
  Git-Bash/MSYS ("MSys/Mingw is not supported") — worked around by downloading
  `riscv32-esp-elf-15.2.0_20251204-x86_64-w64-mingw32.zip` directly from the `tools.json` manifest
  and extracting it into `~/.platformio/packages/toolchain-riscv32-esp/` by hand. Also patched
  `~/.platformio/packages/framework-arduinoespressif32/tools/esp32-arduino-libs/esp32c3/pioarduino-build.py`
  to swap its hardcoded `-flto`/`-flto=auto` for `-fno-lto` (project-level `build_unflags` doesn't
  reach this script's own `env.Append()`, which builds the framework's own `libFrameworkArduino.a`
  — mixing LTO and non-LTO objects was the deeper cause of the symbol-dropping above).
- **Still open**: even with all of the above, the final link fails with core mbedtls symbols
  (`mbedtls_ssl_init`, `mbedtls_ssl_config_defaults`, …) undefined, despite `-lmbedtls` being on
  the link line and `libmbedtls.a` existing on disk. This framework build ships a **second**
  archive, `libmbedtls_2.a` (alongside `-lmbedcrypto -lmbedx509 -leverest -lp256m`), and neither
  satisfies these symbols in testing — looks like this "pioarduino" registry's split-mbedtls
  packaging for ESP32-C3 is simply broken, or needs a link-order/library fix not yet found. Time
  to dig further wasn't there this session, given real hardware was live and waiting — the
  precompiled base firmware (see below) was used for today's real-hardware OTA test instead.

## What is NOT done yet (needs physical hardware — cannot be done from this environment)

- **WP0 (de-risk)**: build + flash `pio run -e xiao_esp32s3 -t upload` (resolve the
  `WiFiClientSecure.h` gap above first if you hit it), bring up the web dashboard, pair one real
  reader, confirm decoded energy on the dashboard/MQTT, 12 h soak. **Do this before trusting
  anything else below** — it proves the upstream crypto/pairing/timing works on our hardware
  before any of our LoRaWAN code is exercised.
- **WP1 hardware**: assemble the XIAO + Wio-SX1262 stack, external antenna.
- **WP2/WP3 field verification**: compiles clean (see above), but has never joined a real
  ChirpStack or shared the radio with live OBI traffic — verify an actual join, then a 24 h
  SHARED-mode soak with a bound reader before trusting it unattended.
- Fill in `include/lorawan_secrets.h` (from `.example`) with a real JoinEUI/AppKey from a
  ChirpStack device registration — register the DevEUI the firmware prints at boot.
- **WP4 ChirpStack side**: create the device profile + paste `chirpstack/codec.js` (manual
  ChirpStack UI/API steps — no ChirpStack access from this environment).
- **WP5 (robustness)**: the missed-beacon/reader-retention soak test over 72 h, duty-cycle
  logging, multi-reader test. `obi_to_lorawan.cpp` logs each radio-borrow event
  (`[lorawan] radio borrowed for N ms`) as a starting point but doesn't yet aggregate a report.
- **WP6 (ops)**: enclosure, antenna mounting, DE provisioning runbook.
- The CC1101 wM-Bus sniffer idea (capture Kamstrup water alongside the OBI electricity reader
  from the same gateway) is **not** part of this pass — it's a genuinely separate RF front-end
  (own SPI CS/GDO0, own decode path) layered on top of an already-nontrivial single-radio
  OBI+LoRaWAN time-share. Prove WP0–WP5 solid first; the fleet's `SeeedXIAOLoRaWAN` project
  already has a CC1101 wM-Bus path (`WATER_METER_MODE=3`) worth reusing as a reference if/when
  this gets picked up.

## Next concrete step

Build it: `cd open_obi_energy_meter && pio run -e xiao_esp32s3`. Fix whatever the compiler finds
(RadioLib API surface has moved between minor versions before), then flash and run WP0.
