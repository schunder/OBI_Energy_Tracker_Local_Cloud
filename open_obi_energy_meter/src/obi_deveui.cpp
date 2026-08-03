#include "obi_deveui.h"

// Fleet DevEUI convention: FE | utility | hardware | low 40 bits of the chip factory ID.
//
// Utility 0x07 = "electricity via OBI-protocol bridge", a new type (0x02 water / 0x03 gas /
// 0x06 combi are already taken by SeeedXIAOLoRaWAN etc.).
//
// Hardware 0x07 = the OBI/heyOBI mains plug (ESP32-C3 + SX1262 on a Ra-03SCH module). This was
// 0x01 until 2026-08-03, copied from the XIAO ESP32-S3 preset with a comment claiming it was "the
// same physical board" -- it is not, and sharing a hardware code with an unrelated board defeats
// the whole point of the byte. Allocated codes: 01 XIAO ESP32-S3, 02 dnt-TRX-ST1, 03 DX-PJ26+LR20,
// 04 dnt KlimaLux, 05 XIAO nRF52840, 06 Ebyte E77, 07 OBI C3 plug.
//
// ⚠️ CHANGING THIS CHANGES THE DevEUI. The device must be re-registered on the Conduit under the
// new DevEUI before it will join again -- see docs/OBI_REMEDIATION_PLAN.md R0.5/R2.1.
#define OBI_DEVEUI_MARKER   0xFE
#define OBI_DEVEUI_UTILITY  0x07
#define OBI_DEVEUI_HARDWARE 0x07

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
