#include "lorawan_uplink.h"
#include "lorawan_config.h"
#include "lorawan_secrets.h"
#include "obi_radio_params.h"
#include "obi_deveui.h"
#include "board_config.h"   // LORA_RXEN_PIN / LORA_DIO2_RFSW — RF-switch scheme for THIS board

// The LoRaWAN transaction reconfigures the shared SX1262 and (per obiRadioRestore in
// obi_to_lorawan.cpp) leaves the RF switch in the wrong state — so the switch must be re-asserted
// BEFORE the LoRaWAN tx too, or the join radiates into a disconnected antenna (radio.begin OK,
// NO_JOIN_ACCEPT, nothing on air). Mirrors main.cpp's begin-time block exactly.
static inline void obiLwAssertRfSwitch() {
#if defined(LORA_RXEN_PIN) && (LORA_RXEN_PIN != RADIOLIB_NC)
  radio.setRfSwitchPins(LORA_RXEN_PIN, RADIOLIB_NC);
#elif LORA_DIO2_RFSW
  radio.setDio2AsRfSwitch(true);
#endif
}

// Maps the LoRaWAN stack onto the SAME `radio` instance the OBI master role uses (see
// obi_radio_params.h) -- there is only ever one physical SX1262 in SHARED mode.
static LoRaWANNode loraNode(&radio, &obilw::Region, obilw::SubBand);

LoRaWANUplink &LoRaWANUplink::getInstance() {
  static LoRaWANUplink instance;
  return instance;
}

void LoRaWANUplink::begin() {
  if (_initialized) return;

  uint64_t devEui = obi_build_deveui();
  Serial.print("[lorawan] DevEUI = "); Serial.println(obi_deveui_to_string(devEui));

  // LoRaWAN 1.0.x: pass NULL for nwkKey so RadioLib stays in rev 0 (1.0) mode -- a non-NULL
  // key here forces 1.1 (rev 1) and the join gets rejected by our 1.0.x ChirpStack profile.
  int16_t state = loraNode.beginOTAA(JoinEUI, devEui, nullptr, (uint8_t *)AppKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[lorawan] beginOTAA failed: %d\n", state);
    _lastTxState = state;
    return;
  }

  // WP1: restore persisted nonces + session so a reboot resumes instead of a fragile rejoin.
  uint8_t nonceBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  if (_persist.loadNonces(nonceBuf, sizeof(nonceBuf)) &&
      loraNode.setBufferNonces(nonceBuf) == RADIOLIB_ERR_NONE) {
    uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    if (_persist.loadSession(sessionBuf, sizeof(sessionBuf))) {
      loraNode.setBufferSession(sessionBuf);
      _restoredFromNvs = true;
      Serial.println("[lorawan] restored nonces + session from NVS (will resume, no air join)");
    }
  }

  _initialized = true;
  Serial.println("[lorawan] stack initialized");
}

void LoRaWANUplink::update() {
  if (!_initialized || _isJoined) return;
  uint32_t now = millis();
  if (_lastJoinTryMillis == 0 || now - _lastJoinTryMillis >= obilw::JoinRetryIntervalMs) attemptJoin();
}

void LoRaWANUplink::attemptJoin() {
  _lastJoinTryMillis = millis();
  Serial.println("[lorawan] OTAA join attempt...");

  const bool freshJoin = !_restoredFromNvs;
  if (freshJoin) loraNode.setADR(obilw::UseADR);

  obiLwAssertRfSwitch();   // ensure the antenna is connected for THIS tx (see note above)

  int16_t state = loraNode.activateOTAA();
  _lastTxState = state;
  _restoredFromNvs = false;

  // Persist nonces after every attempt (success or not) so DevNonce keeps increasing.
  _persist.saveNonces(loraNode.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

  if (state == RADIOLIB_LORAWAN_NEW_SESSION || state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
    _isJoined = true;
    if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      Serial.println("[lorawan] session restored from NVS (no air join)");
    } else {
      Serial.println("[lorawan] joined (new session, air join)");
      loraNode.setADR(obilw::UseADR);
    }
    persistSession();
  } else {
    Serial.print("[lorawan] join failed, return code: ");
    Serial.println(getTxStateString());
  }
}

void LoRaWANUplink::persistSession() {
  _persist.saveSession(loraNode.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
}

bool LoRaWANUplink::sendUplink(const uint8_t *data, size_t len, uint8_t port) {
  if (!_initialized) { Serial.println("[lorawan] send: stack uninitialized"); return false; }
  if (!_isJoined)    { _lastTxState = RADIOLIB_ERR_NETWORK_NOT_JOINED; return false; }

  uint8_t downBuf[255];
  size_t  downLen = 0;
  LoRaWANEvent_t downEvent;
  obiLwAssertRfSwitch();   // antenna connected for the uplink tx (see note at top)
  int16_t state = loraNode.sendReceive((uint8_t *)data, len, port, downBuf, &downLen, false, nullptr, &downEvent);
  _lastTxState = state;

  if (state < RADIOLIB_ERR_NONE) {
    Serial.print("[lorawan] uplink failed, return code: ");
    Serial.println(getTxStateString());
    if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) _isJoined = false;
    return false;
  }

  if (state > 0 && downLen > 0) {
    _downlinkLen = (downLen > DOWNLINK_MAX) ? DOWNLINK_MAX : downLen;
    memcpy(_downlinkData, downBuf, _downlinkLen);
    _downlinkPort = downEvent.fPort;
    _downlinkPending = true;
    Serial.printf("[lorawan] downlink: %u B on FPort %u\n", (unsigned)_downlinkLen, _downlinkPort);
  }

  Serial.println(state > 0 ? "[lorawan] uplink ok, downlink received" : "[lorawan] uplink ok, no downlink");
  // WP1: persist advanced frame counters after every successful uplink so a reboot resumes
  // the session instead of the LNS rejecting stale (replayed) counters.
  persistSession();
  return true;
}

bool    LoRaWANUplink::isJoined() const { return _isJoined; }
int16_t LoRaWANUplink::getLastTxState() const { return _lastTxState; }

String LoRaWANUplink::getTxStateString() const {
  switch (_lastTxState) {
    case RADIOLIB_ERR_NONE:                  return "OK";
    case RADIOLIB_LORAWAN_NEW_SESSION:       return "NEW_SESSION";
    case RADIOLIB_LORAWAN_SESSION_RESTORED:  return "RESTORED";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:    return "NOT_JOINED";
    case RADIOLIB_ERR_NO_JOIN_ACCEPT:        return "NO_JOIN_ACCEPT";
    case RADIOLIB_ERR_UPLINK_UNAVAILABLE:    return "UPLINK_UNAVAILABLE";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:        return "CHIP_NOT_FOUND";
    default:                                 return String(_lastTxState);
  }
}

bool    LoRaWANUplink::hasDownlink() const { return _downlinkPending; }
uint8_t LoRaWANUplink::getDownlinkPort() const { return _downlinkPort; }

size_t LoRaWANUplink::takeDownlink(uint8_t *buf, size_t maxLen) {
  if (!_downlinkPending) return 0;
  size_t n = (_downlinkLen < maxLen) ? _downlinkLen : maxLen;
  memcpy(buf, _downlinkData, n);
  _downlinkPending = false;
  return n;
}
