// payload_codec.h — WP3 uplink frame (FPort obilw::FPort). Mirrored by chirpstack/codec.js --
// keep both in sync if you change this layout.
//
// Frame = [schema_version u8] + up to MaxReadersPerUplink repeats of a 15-byte reader entry:
//   [0]      reader_index  (u8, 0..9)   -- STABLE per-reader slot persisted in NVS ("obislot"),
//                                          NOT the OBI gateway's RAM array index, which can be
//                                          reused by a different reader after a reboot/re-pair.
//   [1..4]   import_Wh     (u32, big-endian)  -- Reader::import_ (pos_power)
//   [5..8]   export_Wh     (u32, big-endian)  -- Reader::export_ (neg_power)
//   [9..12]  power_W       (u32, big-endian)  -- Reader::power; sentinel 0x7FFFFFFF = n/a
//   [13]     battery_raw   (u8)   -- Reader::battery_mV / 20 (the OBI wire's own scale). NOT a
//                                    battery percentage: the reader's cell chemistry/discharge
//                                    curve isn't known, so we transmit the raw scaled reading
//                                    and let the codec/backend decide how to present it.
//   [14]     flags         (u8: bit0 infrared, bit1 lowpower, bit2 timesync)
#pragma once
#include <Arduino.h>
#include "reader.h"

static const uint8_t OBI_LW_SCHEMA_VERSION = 1;
static const size_t  OBI_LW_ENTRY_LEN      = 15;

// Writes the 1-byte schema header at buf[0]. Call once before any obi_lw_append_entry().
// Returns the new offset (1), or 0 if `cap` is too small.
size_t obi_lw_begin_frame(uint8_t *buf, size_t cap);

// Appends one reader's OBI_LW_ENTRY_LEN-byte entry at *offset, bumps *offset. Returns false
// (no-op) if it wouldn't fit within cap.
bool obi_lw_append_entry(uint8_t *buf, size_t cap, size_t *offset, uint8_t reader_index, const Reader &r);
