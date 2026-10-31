// Copyright 2026 Andrey Nemenko.
// Application entry points: modules own hardware, storage, metering and reports.
#ifdef RS485_UART_BRIDGE
#include "rs485_bridge.h"
void setup() { RS485Bridge::setup(); }
void loop() { RS485Bridge::loop(); }
#else
#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif
#include "app/config.h"
#include "app/hardware.h"
#include "app/metering.h"
#include "app/storage.h"
#include "app/zigbee_reporting.h"
#include "Zigbee.h"
#include "zigbee_sleep.h"
#include "../include/version.h"

using namespace App;

// Defer sleep until the coordinator has interviewed all endpoints.
static uint32_t g_join_time_ms = 0;
static bool g_sleep_enabled_runtime = false;

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println("\n╔════════════════════════════════════════════════════════╗");
    Serial.printf("║  ESP32-C6 Zigbee Water Meter v%s              ║\n", firmware::version::kFirmwareVersion.data());
    Serial.printf("║  Build: %s                              ║\n", firmware::version::kBuildTimestamp.data());
    Serial.println("║  Copyright 2026 Andrey Nemenko                       ║");
    Serial.println("╚════════════════════════════════════════════════════════╝\n");

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("*** WOKE FROM DEEP SLEEP (Timer) ***");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("*** WOKE FROM DEEP SLEEP (External) ***");
            break;
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            Serial.println("*** POWER-ON or RESET (not from deep sleep) ***");
            break;
    }

    ZigbeeSleep::begin();
    initHardware();
    checkBootRecovery();
    const auto settings = loadSystemData();
    initSources(settings);
    setupZigbee();

    Serial.println("--- System initialized and running ---");
    StatusLedFlash(0, 30, 0, 1000);
}

void loop() {
    static bool connected_logged = false;
    static uint32_t last_loop_log = 0;
    ZigbeeSleep::AwakeGuard awake;

    uint32_t now = millis();

    updateSources();
#ifdef TEST
    logBatteryAdcForTest();
#endif

    if (Zigbee.connected()) {
        if (!connected_logged) {
            Serial.println("Application: Zigbee.connected() is true. Main logic is now active.");
            connected_logged = true;
            g_join_time_ms = now;
            g_sleep_enabled_runtime = false;
            ZigbeeSleep::allowSleep(false);
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_sleep_enable(false);
            esp_zb_lock_release();
        }

        if (ALLOW_LIGHT_SLEEP && !g_sleep_enabled_runtime && g_join_time_ms != 0 && (now - g_join_time_ms) >= COMMISSIONING_AWAKE_MS) {
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_sleep_enable(true);
            esp_zb_lock_release();
            ZigbeeSleep::allowSleep(true);
            g_sleep_enabled_runtime = true;
            Serial.printf("Zigbee: Sleep enabled after %lu ms commissioning window.\n", (unsigned long)COMMISSIONING_AWAKE_MS);
        }
        handleZigbeeReporting();
        handleAutoSave();
        handleConfigSave();
    } else {
        connected_logged = false;
        ZigbeeSleep::allowSleep(false);
        if (g_sleep_enabled_runtime) {
            esp_zb_lock_acquire(portMAX_DELAY);
            esp_zb_sleep_enable(false);
            esp_zb_lock_release();
            g_sleep_enabled_runtime = false;
        }
    }

    updateStatusIndication();
    checkServiceButton();

    if (now - last_loop_log >= 120000) {
        const uint32_t log_interval = now - last_loop_log;
        last_loop_log = now;

        Serial.printf("System: Loop alive. Connected=%s, Uptime=%lu min, LoopLogInterval=%lu ms\n",
                      Zigbee.connected() ? "YES" : "NO", now / 60000, log_interval);
        Serial.printf("Power: allowed=%s, Zigbee sleep requests=%lu, light sleep wakeups=%lu, slept=%lu ms\n",
                      g_sleep_enabled_runtime ? "YES" : "NO", (unsigned long)ZigbeeSleep::sleepSignalCount(),
                      (unsigned long)ZigbeeSleep::wakeupCount(), (unsigned long)ZigbeeSleep::totalSleepMs());
    }

    awake.release();
    const bool idle = reportsIdle() && Zigbee.connected() && digitalRead(BOOT_BUTTON_PIN) != LOW;
    ZigbeeSleep::waitForWork(idle ? LOOP_IDLE_DELAY : 100);
}

#endif  // RS485_UART_BRIDGE
