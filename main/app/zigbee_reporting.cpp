// Copyright 2026 Andrey Nemenko.
#ifndef RS485_UART_BRIDGE
#include "config.h"
#include "zigbee_reporting.h"
#include "state.h"
#include "hardware.h"

namespace App {

namespace {
// Non-blocking reporting state machine. Each state sends at most one Zigbee
// report, then yields back to loop().
enum ReportState {
    IDLE,
    PENDING_COLD_CONFIG, PENDING_HOT_CONFIG,
    PENDING_COLD_HOURLY, PENDING_HOT_HOURLY,
    PENDING_COLD_VALUE,  PENDING_HOT_VALUE,
    PENDING_HEARTBEAT_COLD
};
static ReportState reportState = IDLE;
static uint32_t nextActionTime = 0;

}  // namespace

void setupZigbee() {
    zigbeeCold.setSource(coldSrc.get());
    zigbeeHot.setSource(hotSrc.get());

    zigbeeCold.begin();
    zigbeeHot.begin();
    zigbeePower.begin();

    Zigbee.addEndpoint(&zigbeeCold);
    Zigbee.addEndpoint(&zigbeeHot);
    Zigbee.addEndpoint(&zigbeePower);

    zigbeeCold.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    zigbeeHot.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    // End-device parent polling continues during light sleep; RAM is retained.
    Zigbee.setRxOnWhenIdle(false);
    esp_zb_sleep_enable(false);

    Serial.printf("Zigbee: Sleep deferred. Threshold=%lu ms\n", (unsigned long)ZIGBEE_SLEEP_THRESHOLD_MS);

    // Arduino's default ED config polls the parent every 3000 ms.
    // The role overload sets the role before the Zigbee task starts.
    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee: CRITICAL ERROR STARTING STACK");
        Serial.println("Data corruption detected or Partition Scheme mismatch.");
        Serial.println("Hold BOOT button during startup to Factory Reset.");
        delay(1000);
        ESP.restart();
        return;
    }
    esp_zb_lock_acquire(portMAX_DELAY);
    ESP_ERROR_CHECK(esp_zb_sleep_set_threshold(ZIGBEE_SLEEP_THRESHOLD_MS));
    esp_zb_sleep_enable(false);
    esp_zb_set_tx_power(TX_POWER);
    esp_zb_lock_release();
}

void handleZigbeeReporting() {
    static uint32_t boot_time = millis();
    static uint32_t last_heartbeat = 0;
    static bool initial_config_sent = false;
    uint32_t now = millis();

    // Keep Zigbee reports serialized so the stack has time to process each
    // attribute update and outgoing report command.
    if (reportState != IDLE && static_cast<int32_t>(now - nextActionTime) >= 0) {
        ReportState currentState = reportState;
        reportState = IDLE;
        switch (currentState) {
            case PENDING_COLD_CONFIG:
                zigbeeCold.reportConfig();
                reportState = PENDING_HOT_CONFIG;
                nextActionTime = now + 200;
                break;
            case PENDING_COLD_HOURLY:
                zigbeeCold.reportHourly();
                if (hotSrc && hotSrc->hasHourChanged()) {
                    reportState = PENDING_HOT_HOURLY;
                    nextActionTime = now + 100;
                }
                break;
            case PENDING_HEARTBEAT_COLD:
                zigbeeCold.reportValue();
                reportState = PENDING_HOT_VALUE;
                nextActionTime = now + 100;
                break;
            case PENDING_COLD_VALUE:
                zigbeeCold.reportValue();
                if (zigbeeHot.shouldReport()) {
                    reportState = PENDING_HOT_VALUE;
                    nextActionTime = now + 100;
                }
                break;

            case PENDING_HOT_CONFIG: zigbeeHot.reportConfig(); break;
            case PENDING_HOT_HOURLY: zigbeeHot.reportHourly(); break;
            case PENDING_HOT_VALUE: zigbeeHot.reportValue(); break;
            default: break;
        }
        return;
    }

    if (reportState != IDLE) return;

    // Report writable configuration after startup so the coordinator receives
    // restored serial numbers and offsets without waiting for a write.
    if (!initial_config_sent && (now - boot_time > 5000)) {
        initial_config_sent = true;
        Serial.println("Zigbee: Reporting initial config...");
        reportState = PENDING_COLD_CONFIG;
        nextActionTime = now;
        return;
    }

    if (zigbeeCold.needsConfigReport()) {
        reportState = PENDING_COLD_CONFIG;
        nextActionTime = now;
        StatusLedSet(30, 30, 30);
        return;
    }

    if (zigbeeHot.needsConfigReport()) {
        reportState = PENDING_HOT_CONFIG;
        nextActionTime = now;
        StatusLedSet(30, 30, 30);
        return;
    }

    if (coldSrc && coldSrc->hasHourChanged()) {
        reportState = PENDING_COLD_HOURLY;
        nextActionTime = now;
        return;
    } else if (hotSrc && hotSrc->hasHourChanged()) {
        reportState = PENDING_HOT_HOURLY;
        nextActionTime = now;
        return;
    }

    bool coldNeeds = zigbeeCold.shouldReport();
    bool hotNeeds  = zigbeeHot.shouldReport();
    bool isHeartbeat = (now - last_heartbeat >= HEARTBEAT_INTERVAL);

    if (isHeartbeat) {
        Serial.println("Scheduling report -> Heartbeat");
        last_heartbeat = now;
        reportState = PENDING_HEARTBEAT_COLD;
        nextActionTime = now;
        StatusLedSet(30, 30, 30);
    } else if (coldNeeds || hotNeeds) {
        if (coldNeeds) {
            reportState = PENDING_COLD_VALUE;
        } else {
            reportState = PENDING_HOT_VALUE;
        }
        nextActionTime = now;
        StatusLedSet(30, 30, 30);
    }

    static uint32_t last_battery = 0;
    if (now - last_battery >= BATTERY_REPORT_INTERVAL || last_battery == 0) {
        last_battery = now;
        if (zigbeeCold.battery_supported()) zigbeeCold.reportBattery();
        if (zigbeeHot.battery_supported())  zigbeeHot.reportBattery();
        uint8_t systemBatteryPercent = zigbeePower.reportStatus();
        if (systemBatteryPercent <= LOW_BATTERY_CRITICAL_PERCENT) {
            Serial.printf(
                "System Power: CRITICAL battery level (%u%%, %lu mV). Consider reducing polling or replacing battery.\n",
                systemBatteryPercent,
                (unsigned long)zigbeePower.lastCalibratedMilliVolts()
            );
        }
    }
}

bool reportsIdle() { return reportState == IDLE; }

}  // namespace App
#endif  // RS485_UART_BRIDGE
