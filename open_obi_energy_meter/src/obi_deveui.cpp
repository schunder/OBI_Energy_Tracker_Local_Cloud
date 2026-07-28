#include "obi_deveui.h"

// New fleet utility code: this is the first "electricity via OBI-protocol bridge" node type
// (distinct from 0x02 water / 0x03 gas / 0x06 combi already used by SeeedXIAOLoRaWAN etc.).
// Hardware code 0x01 matches the existing fleet convention for Seeed XIAO ESP32-S3 + Wio-SX1262
// (same physical board this OBI gateway runs on).
#define OBI_DEVEUI_MARKER   0xFE
#define OBI_DEVEUI_UTILITY  0x07
#define OBI_DEVEUI_HARDWARE 0x01

uint64_t obi_build_deveui() {
  uint64_t mac    = ESP.getEfuseMac();       // 48-bit factory MAC (Espressif OUI)
  uint64_t suffix = mac & 0xFFFFFFFFFFULL;   // low 40 bits
  return ((uint64_t)OBI_DEVEUI_MARKER   << 56) |
         ((uint64_t)OBI_DEVEUI_UTILITY  << 48) |
         ((uint64_t)OBI_DEVEUI_HARDWARE << 40) |
         suffix;
}

String obi_deveui_to_string(uint64_t e) {
  uint8_t b[8];
  for (int i = 0; i < 8; i++) b[i] = (uint8_t)(e >> (56 - 8 * i));
  char buf[24];
  snprintf(buf, sizeof(buf), "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
  return String(buf);
}
