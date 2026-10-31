// Copyright 2026 Andrey Nemenko.
#ifndef RS485_UART_BRIDGE
#include "storage.h"
#include "state.h"
#include <Preferences.h>

namespace App {

namespace {
Preferences prefs;

void saveConfiguration() {
    Serial.println("System: Writing configuration to Flash...");

    auto cs = zigbeeCold.get_serial();
    auto co = zigbeeCold.get_offset();
    auto hs = zigbeeHot.get_serial();
    auto ho = zigbeeHot.get_offset();
    auto cp = zigbeeCold.get_poll_interval_minutes();
    auto hp = zigbeeHot.get_poll_interval_minutes();

    Serial.printf(
        "Config to save -> Cold SN:%lu, Cold Off:%ld, Cold Poll:%u min, Hot SN:%lu, Hot Off:%ld, Hot Poll:%u min\n",
        cs,
        co,
        cp,
        hs,
        ho,
        hp
    );

    prefs.putUInt("sc", cs);
    prefs.putInt("oc", co);
    prefs.putUInt("pc", cp);

    prefs.putUInt("sh", hs);
    prefs.putInt("oh", ho);
    prefs.putUInt("ph", hp);

    if (zigbeeCold.readings_valid()) {
        prefs.putULong64("cl", zigbeeCold.get_val());
    } else {
        Serial.println("Config save: skipped cold reading because source data is not valid yet.");
    }

    if (zigbeeHot.readings_valid()) {
        prefs.putULong64("hl", zigbeeHot.get_val());
    } else {
        Serial.println("Config save: skipped hot reading because source data is not valid yet.");
    }
}
}  // namespace

StoredSettings loadSystemData() {
    prefs.begin("water", false);
    // Preserve the existing NVS namespace, keys and defaults.
    StoredSettings settings{};
    settings.cold.serial = prefs.getUInt("sc", 0);
    settings.hot.serial = prefs.getUInt("sh", 0);
    settings.cold.liters = prefs.getULong64("cl", 0);
    settings.hot.liters = prefs.getULong64("hl", 0);
    settings.cold.offset = prefs.getInt("oc", 0);
    settings.hot.offset = prefs.getInt("oh", 0);
    settings.cold.pollMinutes = prefs.getUInt("pc", 0);
    settings.hot.pollMinutes = prefs.getUInt("ph", 0);
    return settings;
}

void handleAutoSave() {
    static uint32_t last_save = 0;
    if (millis() - last_save >= 900000) {
        last_save = millis();
        saveConfiguration();
    }
}

void handleConfigSave() {
    if (zigbeeCold.isConfigDirty() || zigbeeHot.isConfigDirty()) {
        saveConfiguration();
        zigbeeCold.clearConfigDirty();
        zigbeeHot.clearConfigDirty();
    }
}

}  // namespace App
#endif  // RS485_UART_BRIDGE
