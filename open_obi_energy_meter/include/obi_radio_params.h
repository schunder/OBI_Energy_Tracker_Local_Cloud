// obi_radio_params.h — reversed OBI radio PHY constants + the shared SX1262 handle.
//
// SHARED radio mode (see obi_to_lorawan.cpp) time-shares ONE physical SX1262 between the
// OBI master role and periodic LoRaWAN uplinks. Every LoRaWAN transaction reconfigures the
// chip's frequency/SF/sync word/etc, so these constants must be re-applied afterwards to
// resume the OBI link exactly as it was. Kept in one header so main.cpp's boot-time
// radio.begin() and obi_to_lorawan.cpp's post-uplink restore can never drift apart.
#pragma once
#include <RadioLib.h>

#define OBI_FREQ_MHZ   869.5f
#define OBI_BW_KHZ     500.0f
#define OBI_SF_DEFAULT 7             // reader stock/default SF; runtime SF lives in g_loraSF/g_liveSF below
#define OBI_CR         5
#define OBI_SYNCWORD   0x12          // RadioLib private -> SX126x 0x1424
#define OBI_TXPWR_DBM  22
#define OBI_PREAMBLE   12

extern SX1262 radio;                 // the single shared SX1262 instance (defined in main.cpp)
void onDio1();                       // DIO1 ISR (IRAM_ATTR on its definition in main.cpp) — reattached after a LoRaWAN tx

// Runtime OBI spreading factor (upstream SF7/SF9 feature; defined in main.cpp). g_liveSF is the SF the
// radio is ACTUALLY running now. obiRadioRestore() restores to g_liveSF so the OBI link resumes on the
// SF the user selected, not a hard-coded one.
extern uint8_t g_loraSF;
extern uint8_t g_liveSF;
