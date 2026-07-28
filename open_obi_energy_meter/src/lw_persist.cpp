#include "lw_persist.h"
#include <Preferences.h>

namespace {
constexpr const char *NS          = "obilw";
constexpr const char *KEY_NONCES  = "nonces";
constexpr const char *KEY_SESSION = "session";
}  // namespace

bool LwPersist::loadNonces(uint8_t *buf, size_t len) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  bool ok = (p.getBytesLength(KEY_NONCES) == len) && (p.getBytes(KEY_NONCES, buf, len) == len);
  p.end();
  return ok;
}

void LwPersist::saveNonces(const uint8_t *buf, size_t len) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putBytes(KEY_NONCES, buf, len);
  p.end();
}

bool LwPersist::loadSession(uint8_t *buf, size_t len) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  bool ok = (p.getBytesLength(KEY_SESSION) == len) && (p.getBytes(KEY_SESSION, buf, len) == len);
  p.end();
  return ok;
}

void LwPersist::saveSession(const uint8_t *buf, size_t len) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putBytes(KEY_SESSION, buf, len);
  p.end();
}

void LwPersist::clear() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.clear();
  p.end();
}
