// lorawan_config.h — LoRaWAN uplink-path tunables (WP2/WP3). Keys live in lorawan_secrets.h
// (git-ignored; copy from lorawan_secrets.h.example).
#pragma once
#include <RadioLib.h>

namespace obilw {

const LoRaWANBand_t Region = EU868;
const uint8_t  SubBand = 0;              // set to match the municipal ChirpStack channel plan

const uint8_t  FPort        = 10;        // energy uplink (see payload_codec.h)
const uint8_t  ControlFPort = 20;        // downlink: set a reader's OBI upload interval (WP4, optional)

// SHARED radio mode borrows the single SX1262 from the OBI master role for one join/uplink
// attempt every this many ms, right after a beacon TX (see obi_to_lorawan.cpp). The OBI readers
// report every upload_interval seconds (default 25 here, up to 300 stock) and tolerate a handful
// of missed 1 Hz beacons via their retry/backoff, so keep this well above a few seconds and
// aggregate readers into one frame rather than uplinking more often.
const uint32_t UplinkPeriodMs      = 300000UL;   // >= 300 s
const uint32_t JoinRetryIntervalMs = 30000UL;
const uint8_t  MaxReadersPerUplink = 3;          // caps frame size; freshest readers win (see .cpp)

const bool UseADR = true;   // let the network negotiate DR; self-backs-off to more range on lost downlinks

}  // namespace obilw
