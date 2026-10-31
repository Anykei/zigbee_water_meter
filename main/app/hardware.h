#pragma once

#include "utils.h"

namespace App {
inline void StatusLedSet(uint8_t r, uint8_t g, uint8_t b) { Utils::setLed(r, g, b); }
inline void StatusLedFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) { Utils::flashLed(r, g, b, ms); }
void initHardware();
void checkBootRecovery();
void checkServiceButton();
void updateStatusIndication();
#ifdef TEST
void logBatteryAdcForTest();
#endif
}  // namespace App
