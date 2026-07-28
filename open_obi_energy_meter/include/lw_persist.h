// lw_persist.h — NVS persistence for the LoRaWAN join nonces + session (WP2). Lets a reboot
// resume the existing session instead of a fragile rejoin, and keeps DevNonce monotonically
// increasing across reboots (an LNS rejects a reused DevNonce). Pattern copied from the fleet's
// LoRaWANsensors/*/LwPersist -- separate NVS namespace ("obilw") so it can't collide with the
// OBI base firmware's own Preferences namespaces (obiuuid/obiassign/obiival/obiname).
#pragma once
#include <Arduino.h>

class LwPersist {
public:
  bool loadNonces(uint8_t *buf, size_t len);
  void saveNonces(const uint8_t *buf, size_t len);
  bool loadSession(uint8_t *buf, size_t len);
  void saveSession(const uint8_t *buf, size_t len);
  void clear();
};
