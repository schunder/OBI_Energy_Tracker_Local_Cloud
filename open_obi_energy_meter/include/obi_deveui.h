// obi_deveui.h — fleet DevEUI convention (see LoRaWANsensors/*/DevEui.h across the municipal
// fleet): FE | utility | hardware | chip factory-MAC low 40 bits. Locally-administered marker
// 0xFE never collides with a real IEEE OUI; the chip MAC suffix makes it unique per unit with
// zero manual serial tracking. Printed at boot so it can be registered on the Conduit/ChirpStack.
#pragma once
#include <Arduino.h>

uint64_t obi_build_deveui();
String   obi_deveui_to_string(uint64_t e);
