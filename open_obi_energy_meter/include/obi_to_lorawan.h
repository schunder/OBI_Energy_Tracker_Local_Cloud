// obi_to_lorawan.h — glue: OBI energy readings -> LoRaWAN uplink (WP3), SHARED radio mode.
#pragma once
#include <Arduino.h>
#include "reader.h"

// Prepare the LoRaWAN OTAA stack. Call once from setup(), AFTER radio.begin() with the OBI
// PHY params -- this only builds LoRaWANNode state + restores a persisted session; it does not
// touch the radio's live configuration.
void obi_lorawan_setup();

// SHARED radio mode: called from loraTask right after a beacon TX (see main.cpp). No-op unless
// obilw::UplinkPeriodMs has elapsed since the last attempt. When due, it borrows the single
// physical SX1262 for one join/uplink attempt (aggregating up to obilw::MaxReadersPerUplink
// readers' latest energy data), then restores the OBI PHY and resumes RX before returning.
// `readers`/`maxReaders` = the OBI gateway's live reader table (main.cpp's readers[MAX_READERS]).
void obi_lorawan_tick(Reader *readers, int maxReaders, uint32_t nowMs);

// Runtime enable gate (default OFF, persisted in NVS). The uplink does NOT touch the radio until
// enabled — flashing the fork boots exactly like the base firmware with LoRaWAN dormant. Enable
// deliberately once ready to observe a join (web POST /api/lw en=1). See obi_to_lorawan.cpp.
void obi_lorawan_set_enabled(bool on);
bool obi_lorawan_enabled();
bool obi_lorawan_joined();
