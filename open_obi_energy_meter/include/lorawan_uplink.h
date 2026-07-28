// lorawan_uplink.h — RadioLib LoRaWANNode OTAA wrapper (WP2). Binds to the SAME physical SX1262
// the OBI master role already owns (SHARED radio mode, see obi_to_lorawan.cpp) -- it does not
// create a second radio instance. Pattern mirrors the fleet's proven LoRaWANsensors/*/LoRaWANManager.
#pragma once
#include <Arduino.h>
#include <RadioLib.h>
#include "lw_persist.h"

class LoRaWANUplink {
public:
  static LoRaWANUplink &getInstance();

  // Bind OTAA credentials to the shared `radio` (already begin()'d with the OBI PHY params by
  // main.cpp's setup()) and restore a persisted session from NVS if one exists. Does NOT change
  // the radio's live configuration.
  void begin();

  // Join-retry housekeeping; call before attempting an uplink. No-op once joined.
  void update();

  // Blocking OTAA uplink (~2.1 s: TX + RX1 + RX2). Caller owns the radio for the duration.
  bool sendUplink(const uint8_t *data, size_t len, uint8_t port);

  bool    isJoined() const;
  int16_t getLastTxState() const;
  String  getTxStateString() const;

  // Class A: a downlink can only arrive in the RX window right after an uplink, so this is
  // populated as a side effect of sendUplink().
  bool    hasDownlink() const;
  uint8_t getDownlinkPort() const;
  size_t  takeDownlink(uint8_t *buf, size_t maxLen);

private:
  LoRaWANUplink() = default;
  void attemptJoin();
  void persistSession();

  LwPersist _persist;
  bool _restoredFromNvs = false;
  bool _isJoined        = false;
  bool _initialized      = false;
  uint32_t _lastJoinTryMillis = 0;
  int16_t  _lastTxState       = RADIOLIB_ERR_NONE;

  static const size_t DOWNLINK_MAX = 64;
  uint8_t _downlinkData[DOWNLINK_MAX];
  size_t  _downlinkLen     = 0;
  uint8_t _downlinkPort    = 0;
  bool    _downlinkPending = false;
};
