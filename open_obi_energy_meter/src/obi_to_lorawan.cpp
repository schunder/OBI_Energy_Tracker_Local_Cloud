#include "obi_to_lorawan.h"
#include <algorithm>
#include <Preferences.h>
#include "obi_radio_params.h"
#include "lorawan_config.h"
#include "lorawan_uplink.h"
#include "payload_codec.h"

// ---- persisted reader_index slots (0..9) -----------------------------------------------------
// The WP3 wire format's reader_index is a SMALL STABLE id, not the OBI gateway's RAM array
// index -- a reboot or re-pair can hand a different reader the same RAM slot, which would
// silently swap two readers' identities in ChirpStack/Grafana. First-seen readers get the next
// free id 0..9 (matches the OBI protocol's own <=10-readers-per-gateway cap); persisted in NVS
// so it survives reboots. Separate namespace from the base firmware's own Preferences uses.
static uint8_t obiSlotFor(const uint8_t handle[3]) {
  char k[8]; snprintf(k, sizeof k, "%02x%02x%02x", handle[0], handle[1], handle[2]);

  Preferences p;
  p.begin("obislot", true);
  bool have = p.isKey(k);
  uint8_t slot = have ? p.getUChar(k, 0) : 0;
  p.end();
  if (have) return slot;

  p.begin("obislot", false);
  uint8_t next = p.getUChar("next", 0);
  slot = next % 10;
  p.putUChar(k, slot);
  p.putUChar("next", (uint8_t)((next + 1) % 10));
  p.end();
  Serial.printf("[lorawan] assigned reader_index %u to %s\n", slot, k);
  return slot;
}

// Restore the OBI PHY after a LoRaWAN transaction reconfigured the shared SX1262. Mirrors the
// args main.cpp's setup() passed to radio.begin() (see obi_radio_params.h) via individual
// setters -- cheaper than a full begin() and skips redoing the SPI/ECDH bring-up.
static void obiRadioRestore() {
  radio.standby();
  radio.setFrequency(OBI_FREQ_MHZ);
  radio.setBandwidth(OBI_BW_KHZ);
  radio.setSpreadingFactor(OBI_SF);
  radio.setCodingRate(OBI_CR);
  radio.setSyncWord(OBI_SYNCWORD);
  radio.setOutputPower(OBI_TXPWR_DBM);
  radio.setPreambleLength(OBI_PREAMBLE);
#if defined(LORA_RXEN_PIN) && (LORA_RXEN_PIN != RADIOLIB_NC)
  radio.setRfSwitchPins(LORA_RXEN_PIN, RADIOLIB_NC);
#elif LORA_DIO2_RFSW
  radio.setDio2AsRfSwitch(true);
#endif
  radio.setCRC(2);
  radio.setCurrentLimit(140.0);
  radio.setRxBoostedGainMode(true);
  radio.setDio1Action(onDio1);
  radio.startReceive();
}

// Runtime enable gate, persisted in NVS (namespace "obilw", key "en"), DEFAULT OFF.
// Why default-off + runtime toggle: the SHARED-mode LoRaWAN transaction time-shares the single
// SX1262 on a blocking, multi-second radio operation from the high-priority loraTask. On the
// SINGLE-CORE C3 (OBI_BOARD_OBI_C3) that first-boot transaction crashed the device (see
// LORAWAN.md — the plan itself flags the C3 as the "hardest host" for SHARED). Keeping it
// OFF by default means flashing the fork boots exactly like the base firmware (OBI electricity path
// unchanged, LoRaWAN dormant) so the app marks itself valid and the OTA rollback safety net isn't
// tripped. Turn it on deliberately (web POST /api/lw en=1) once ready to observe a join.
static bool     g_lwEnabled = false;
static uint32_t g_lastUplinkAttemptMs = 0;
static uint32_t g_uplinkAttempts      = 0;   // diagnostic: how many times we've borrowed the radio

// No LoRaWAN radio op in the first minute of uptime: let the OBI link + WiFi settle, and let the
// Arduino core mark this app image valid (cancel the pending-verify rollback) BEFORE we touch the
// radio in the risky shared path — so even if the transaction still misbehaves, one clean boot has
// already happened and the device won't roll back out from under us mid-debug.
static const uint32_t kLwStartupGraceMs = 60000;

void obi_lorawan_setup() {
  Preferences p;
  if (p.begin("obilw", true)) { g_lwEnabled = p.getBool("en", false); p.end(); }
  Serial.printf("[lorawan] uplink %s at boot (POST /api/lw en=1 to enable)\n", g_lwEnabled ? "ENABLED" : "disabled");
  // NOTE: LoRaWANUplink::begin() is intentionally NOT called here. It (and every other radio touch)
  // is deferred to obi_lorawan_tick(), which runs on the loraTask that OWNS the radio — never from
  // setup()/web task, and never within the startup grace window. Keeps all radio access on one core.
}

// Toggle the uplink at runtime (web/UART). Persisted so it survives reboots. Only flips the flag +
// NVS — the actual LoRaWAN init happens lazily on the loraTask's next tick (correct core for the
// radio). Safe to call from the web task (core 0).
void obi_lorawan_set_enabled(bool on) {
  g_lwEnabled = on;
  Preferences p;
  if (p.begin("obilw", false)) { p.putBool("en", on); p.end(); }
  Serial.printf("[lorawan] uplink %s (persisted)\n", on ? "ENABLED" : "disabled");
}
bool obi_lorawan_enabled() { return g_lwEnabled; }
bool obi_lorawan_joined()  { return LoRaWANUplink::getInstance().isJoined(); }

String   obi_lorawan_last_state() { return LoRaWANUplink::getInstance().getTxStateString(); }
uint32_t obi_lorawan_attempts()   { return g_uplinkAttempts; }

void obi_lorawan_tick(Reader *readers, int maxReaders, uint32_t nowMs) {
  if (!g_lwEnabled) return;                          // runtime-disabled -> never touch the radio
  if (nowMs < kLwStartupGraceMs) return;            // startup grace (see kLwStartupGraceMs)
  if (g_lastUplinkAttemptMs != 0 && nowMs - g_lastUplinkAttemptMs < obilw::UplinkPeriodMs) return;
  g_lastUplinkAttemptMs = nowMs;
  g_uplinkAttempts++;

  // Lazy one-time init on the loraTask (radio's owning core). begin() self-guards on _initialized,
  // so this is a cheap no-op after the first successful setup.
  LoRaWANUplink &lw = LoRaWANUplink::getInstance();
  lw.begin();

  // Detach the OBI DIO1 ISR for the duration: it must not fire (or hand a LoRaWAN RX1/RX2 frame
  // to handleRx() as if it were an OBI packet) while the radio belongs to the LoRaWAN stack.
  // clearDio1Action() disables ONLY the OBI ISR path; LoRaWANNode drives DIO1 itself internally
  // during sendReceive(). VERIFY this call against the pinned RadioLib version (7.0.2) if the
  // API has moved -- see the implementation plan's own "verify against pinned version" caveat.
  radio.clearDio1Action();

  uint32_t t0 = millis();
  lw.update();   // join-retry housekeeping; no-op once joined

  if (lw.isJoined()) {
    uint8_t buf[1 + 3 * OBI_LW_ENTRY_LEN];   // header + up to MaxReadersPerUplink entries
    size_t  off = obi_lw_begin_frame(buf, sizeof buf);
    int     sent = 0;

    // Pick the readers with the freshest energy data, capped at MaxReadersPerUplink -- "aggregate
    // the latest value(s)" (plan WP3.2), so a reader that reported since the last uplink wins over
    // one that's been quiet.
    static const int kMaxCandidates = 64;
    Reader *candidates[kMaxCandidates];
    int n = 0;
    int scan = maxReaders < kMaxCandidates ? maxReaders : kMaxCandidates;
    for (int i = 0; i < scan; i++)
      if (readers[i].used && readers[i].assigned && readers[i].haveData) candidates[n++] = &readers[i];
    std::sort(candidates, candidates + n, [](Reader *a, Reader *b) { return a->lastEnergyMs > b->lastEnergyMs; });

    for (int i = 0; i < n && sent < (int)obilw::MaxReadersPerUplink; i++) {
      uint8_t slot = obiSlotFor(candidates[i]->handle);
      if (obi_lw_append_entry(buf, sizeof buf, &off, slot, *candidates[i])) sent++;
    }

    if (sent > 0) {
      Serial.printf("[lorawan] uplink: %d reader(s), %u B\n", sent, (unsigned)off);
      lw.sendUplink(buf, off, obilw::FPort);

      // WP4 (optional): remote set-upload-interval. Downlink on ControlFPort, 3 bytes:
      // [0]=reader_index [1..2]=interval_s (u16 BE).
      if (lw.hasDownlink() && lw.getDownlinkPort() == obilw::ControlFPort) {
        uint8_t d[64];
        size_t  dl = lw.takeDownlink(d, sizeof d);
        if (dl >= 3) {
          uint16_t secs = ((uint16_t)d[1] << 8) | d[2];
          for (int i = 0; i < maxReaders; i++)
            if (readers[i].used && obiSlotFor(readers[i].handle) == d[0]) {
              gw_request_interval(readers[i].handle, secs);
              Serial.printf("[lorawan] downlink: reader_index %u interval -> %us\n", d[0], secs);
            }
        }
      }
    } else {
      Serial.println("[lorawan] no reader data yet -- join/keepalive only this period");
    }
  }

  obiRadioRestore();
  Serial.printf("[lorawan] radio borrowed for %lu ms (attempt #%lu)\n",
               (unsigned long)(millis() - t0), (unsigned long)g_uplinkAttempts);
}
