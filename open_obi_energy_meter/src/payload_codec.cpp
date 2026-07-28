#include "payload_codec.h"

static void wbe32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

size_t obi_lw_begin_frame(uint8_t *buf, size_t cap) {
  if (cap < 1) return 0;
  buf[0] = OBI_LW_SCHEMA_VERSION;
  return 1;
}

bool obi_lw_append_entry(uint8_t *buf, size_t cap, size_t *offset, uint8_t reader_index, const Reader &r) {
  if (*offset + OBI_LW_ENTRY_LEN > cap) return false;
  uint8_t *e = buf + *offset;
  e[0] = reader_index;
  wbe32(e + 1, r.import_);
  wbe32(e + 5, r.export_);
  wbe32(e + 9, r.power);           // 0x7FFFFFFF passes through unchanged (n/a sentinel)
  e[13] = (uint8_t)(r.battery_mV / 20);
  e[14] = r.flags & 0x07;
  *offset += OBI_LW_ENTRY_LEN;
  return true;
}
