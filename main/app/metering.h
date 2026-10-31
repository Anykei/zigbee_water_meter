#pragma once

#include "storage.h"

namespace App {
void initMeterHardware();
void initSources(const StoredSettings& settings);
void updateSources();
}  // namespace App
