#pragma once

#include <cstdint>

namespace App {
struct StoredMeterSettings {
    uint32_t serial;
    uint64_t liters;
    int32_t offset;
    uint32_t pollMinutes;
};
struct StoredSettings {
    StoredMeterSettings cold;
    StoredMeterSettings hot;
};
StoredSettings loadSystemData();
void handleAutoSave();
void handleConfigSave();
}  // namespace App
