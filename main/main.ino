/*
 * Copyright 2026 Andrey Nemenko
 *
 * Main application entry point for the ESP32-C6 Zigbee Water Meter.
 * This file handles hardware initialization, Zigbee stack configuration,
 * and the main event loop using a non-blocking architecture.
 * 
 * Version: 1.0.0
 * Release Date: 2026-02-09
 */

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>
#include "nvs_flash.h"
#include "esp_partition.h"
#include <memory> // For std::unique_ptr

// Abstraction Layers
#include "utils.h"
#include "zigbee_water_meter.h"
#include "hwi_streams/rs485_stream.h"
#include "drivers/driver_factory.h"
#include "sources/factory_source.h"

/* --- VERSION --- */
#include "include/version.h"

/* --- HARDWARE CONFIGURATION --- */
#define RGB_LED_PIN      8
#define BOOT_BUTTON_PIN  9
#define RS485_POWER_PIN  18
#define RS485_RX         21
#define RS485_TX         20
#define RS485_EN         19
#define RS485_BAUD       9600
#define RS485_CONFIG     SERIAL_8N1
#define PULSE_COLD_PIN   10
#define PULSE_HOT_PIN    11
#define BATTERY_ADC_PIN   34

/* --- ZIGBEE CONFIGURATION --- */
#define MODEL_ID "C6_WATER_METER"
#define MANUFACTURER_NAME "MuseLab"
#define TX_POWER 20
#define RECCONNECT_TIMEOUT 60000

/* --- APPLICATION CONFIGURATION --- */
constexpr bool kEnableTestIntervals = false; // Set to true for fast hourly/daily reports (10s/20s)

// constexpr Source::SourceType COLD_TYPE = Source::SourceType::Test;
// constexpr Source::SourceType HOT_TYPE  = Source::SourceType::Test;
// constexpr uint32_t COLD_POOL_INTERVAL = 3000; // Интервал опроса для холодного канала (мс) Test
// constexpr uint32_t HOT_POOL_INTERVAL  = 3000; // Интервал опроса для горячего канала (мс) Test

// constexpr uint32_t HEARTBEAT_INTERVAL = 60000; // Heartbeat interval for HA (ms)
// constexpr uint32_t BATTERY_REPORT_INTERVAL = 60000; // Interval for reporting battery status (ms)

/* PRODUCT CONFIGURATION */
constexpr uint32_t HEARTBEAT_INTERVAL = 60000 * 30; // Heartbeat interval for HA (ms)
constexpr uint32_t BATTERY_REPORT_INTERVAL = 60000 * 30; // Interval for reporting battery status (ms)
constexpr uint32_t COLD_POOL_INTERVAL = 60000 * 30; // Polling interval for cold channel (ms)
constexpr uint32_t HOT_POOL_INTERVAL  = 60000 * 30; // Polling interval for hot channel (ms)
constexpr uint32_t DEEP_SLEEP_THRESHOLD = 60; // Time in seconds before entering deep sleep when idle
constexpr uint32_t LOOP_IDLE_DELAY = 15000; // Main loop idle delay (ms)

constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;

constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;

constexpr bool NEED_RS485 = (COLD_TYPE == Source::SourceType::Smart || HOT_TYPE == Source::SourceType::Smart);

/* --- GLOBAL OBJECTS --- */
Preferences prefs;
std::unique_ptr<RS485Stream> rs485Bus = nullptr; 

// Zigbee Endpoints
ZigbeeWaterMeter zigbeeCold(1, true); 
ZigbeeWaterMeter zigbeeHot(2, false);

// Interfaces (Pointers)
std::unique_ptr<Driver::SmartMeterDriver> coldDrv = nullptr;
std::unique_ptr<Driver::SmartMeterDriver> hotDrv = nullptr;
std::unique_ptr<Source::WaterSource> coldSrc = nullptr;
std::unique_ptr<Source::WaterSource> hotSrc = nullptr;

/* --- ZIGBEE EVENT HANDLER --- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    if (message == nullptr) {
        return ESP_OK;  // Safety: некоторые callback могут приходить без данных
    }

    // Handle attribute write commands from coordinator
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        auto *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        Utils::setLed(0, 30, 30);
        for (auto ep : Zigbee.ep_objects) {
            if (msg->info.dst_endpoint == ep->getEndpoint()) {
                static_cast<ZigbeeWaterMeter*>(ep)->handleAttributeWrite(msg);
                break;
            }
        }
        return ESP_OK;
    }

    // Handle sleep signal (End Device only)
    // NOTE: For ED, sleep is managed automatically by the stack.
    // This callback is informational and should NOT call esp_zb_sleep_now().
    if (callback_id == ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        // Stack will automatically enter light sleep between polls.
        // We don't disable RS485 power here because:
        // 1. Light sleep keeps peripherals powered
        // 2. We need RS485 ready when device wakes for polling
        // 3. Manual power control here causes race conditions
        return ESP_OK;
    }

    // Handle Zigbee application signals (connection, steering, leave)
    // Только для этих callback_id message является esp_zb_app_signal_t*
    esp_zb_app_signal_t *signal = (esp_zb_app_signal_t *)message;
    if (signal->p_app_signal == nullptr) {
        return ESP_OK;  // Защита от невалидного сигнала
    }

    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*signal->p_app_signal;
    esp_err_t sig_status = signal->esp_err_status;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_LEAVE:
            Serial.println("Zigbee: Connection lost (Leave). Rebooting...");
            Utils::flashLed(50, 0, 0, 500);
            if constexpr (NEED_RS485) digitalWrite(RS485_POWER_PIN, LOW);
            delay(100);
            esp_restart();
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (sig_status == ESP_OK) {
                Serial.println("Zigbee: Connected successfully. Enabling RS485 power.");
                if constexpr (NEED_RS485) digitalWrite(RS485_POWER_PIN, HIGH);
            } else {
                Serial.printf("Zigbee: Steering failed with status 0x%x\n", sig_status);
            }
            break;

        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            Serial.println("Zigbee: Device already commissioned, skipping pairing.");
            if constexpr (NEED_RS485) digitalWrite(RS485_POWER_PIN, HIGH);
            break;

        default:
            // Игнорируем другие сигналы (Production Update, Device Announce и т.д.)
            break;
    }
    
    return ESP_OK;
}

/* --- INTERRUPTS (PulseSource Only) --- */
void IRAM_ATTR isr_cold() { 
    if(coldSrc) { // Проверка типа неявна в static_cast
        static_cast<Source::PulseSource*>(coldSrc.get())->increment(); 
    }
}

void IRAM_ATTR isr_hot() { 
    if(hotSrc) {
        static_cast<Source::PulseSource*>(hotSrc.get())->increment(); 
    }
}

// Saves the current configuration and meter readings to NVS.
void saveConfiguration() {
    Serial.println("System: Writing configuration to Flash...");
    
    auto cs = zigbeeCold.get_serial();
    auto co = zigbeeCold.get_offset();
    auto hs = zigbeeHot.get_serial();
    auto ho = zigbeeHot.get_offset();  

    Serial.printf("Config to save -> Cold SN:%lu, Cold Off:%ld, Hot SN:%lu, Hot Off:%ld\n", cs, co, hs, ho);

    // Save Cold channel settings
    prefs.putUInt("sc", cs);
    prefs.putInt("oc", co);
    
    // Save Hot channel settings
    prefs.putUInt("sh", hs);
    prefs.putInt("oh", ho);
    
    // Save current liters (since NVS is already open for writing)
    prefs.putULong64("cl", zigbeeCold.get_val());
    prefs.putULong64("hl", zigbeeHot.get_val());
}

// Emergency recovery: Erase all data if button is held at boot
void checkBootRecovery() {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        Serial.println("\n!!! BOOT BUTTON HELD - RECOVERY MODE !!!");
        Utils::setLed(50, 0, 0); // Red warning
        delay(3000); // Wait to confirm intention
        
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            Serial.println("Erasing NVS...");
            nvs_flash_erase();
            nvs_flash_init();

            Serial.println("Erasing Zigbee Storage...");
            const esp_partition_t* zb_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "zb_storage");
            if (zb_part) {
                esp_partition_erase_range(zb_part, 0, zb_part->size);
                Serial.println("Done.");
            } else {
                Serial.println("Partition 'zb_storage' not found!");
            }

            Utils::flashLed(0, 50, 0, 1000); // Green success
            Serial.println("Restarting...");
            ESP.restart();
        }
    }
}

// Standard Arduino setup function.
void setup() {
    Serial.begin(115200);
    delay(100); // Allow time for serial to initialize before printing boot messages
    
    // Print firmware version
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
    
    initHardware();    // Layer 0: Hardware and Power
    checkBootRecovery(); // Emergency Reset Check
    loadSystemData();  // Layer 1: Storage (NVS)
    initSources();     // Layer 2: Drivers and Sources
    setupZigbee();     // Layer 3: Network Stack
    
    Serial.println("--- System initialized and running ---");
    Utils::flashLed(0, 30, 0, 1000); // Final green signal
}

void initHardware() {
    Serial.begin(115200);
    Utils::setLed(30, 0, 0); // Статус: Загрузка

    // Шина данных
    if constexpr (NEED_RS485) {
        pinMode(RS485_POWER_PIN, OUTPUT);
        digitalWrite(RS485_POWER_PIN, HIGH); // Включаем питание шины

        rs485Bus = std::make_unique<RS485Stream>(&Serial1, RS485_EN);
        rs485Bus->begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
        rs485Bus->setTimeout(300);
    }
    
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
}

void loadSystemData() {
    prefs.begin("water", false);
    // Open storage. Data reading is done in initSources for localization.
}

void initSources() {
    // 1. Read settings from memory
    uint32_t c_sn  = prefs.getUInt("sc", 0);
    uint32_t h_sn  = prefs.getUInt("sh", 0);
    uint64_t c_lit = prefs.getULong64("cl", 0);
    uint64_t h_lit = prefs.getULong64("hl", 0);
    int32_t  c_off = prefs.getInt("oc", 0);
    int32_t  h_off = prefs.getInt("oh", 0);
    
// Loaded config -> Cold SN:10128442, Cold Off:0, Hot SN:10128939, Hot Off:0

    Serial.printf("Loaded config -> Cold SN:%lu, Cold Off:%ld, Hot SN:%lu, Hot Off:%ld\n", c_sn, c_off, h_sn, h_off);
    
    // 2. Create Drivers (Protocol Layer)
    if constexpr (COLD_TYPE == Source::SourceType::Smart) {
        coldDrv.reset(Driver::DriverFactory::create(COLD_DRV_MODEL, rs485Bus.get(), c_sn));
        if (coldDrv) coldDrv->setLogger(&Serial); // Передаем raw pointer для логирования
    } else {
        Serial.println("Cold driver not created");
    }

    if constexpr (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv.reset(Driver::DriverFactory::create(HOT_DRV_MODEL, rs485Bus.get(), h_sn));
        if (hotDrv) hotDrv->setLogger(&Serial);
    } else {
        Serial.println("Hot driver not created");
    }

    // 3. Create Sources (Logic Layer)
    coldSrc.reset(Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv.get()));
    hotSrc.reset(Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv.get()));

    // 4. Fine Tuning (Offsets & Start)
    if (coldSrc) { 
        coldSrc->setPollInterval(COLD_POOL_INTERVAL);
        coldSrc->setOffset(c_off); 
        coldSrc->setTestMode(kEnableTestIntervals);
        coldSrc->setSerialNumber(c_sn);
        coldSrc->begin();
        if constexpr (COLD_TYPE == Source::SourceType::Pulse) {
            attachInterrupt(digitalPinToInterrupt(PULSE_COLD_PIN), isr_cold, FALLING);
        }
    } else {
        Serial.println("Cold source not created");
    }
    if (hotSrc) { 
        hotSrc->setPollInterval(HOT_POOL_INTERVAL);
        hotSrc->setOffset(h_off); 
        hotSrc->setTestMode(kEnableTestIntervals);
        hotSrc->setSerialNumber(h_sn);
        hotSrc->begin();
        if constexpr (HOT_TYPE == Source::SourceType::Pulse) {
            attachInterrupt(digitalPinToInterrupt(PULSE_HOT_PIN), isr_hot, FALLING);
        }
    } else {
        Serial.println("Hot source not created");   
    }
}

void setupZigbee() {
    // Bind sources to endpoints
    zigbeeCold.setSource(coldSrc.get());
    zigbeeHot.setSource(hotSrc.get());

    // Register endpoints in the stack
    zigbeeCold.begin(); 
    zigbeeHot.begin();
    Zigbee.addEndpoint(&zigbeeCold); 
    Zigbee.addEndpoint(&zigbeeHot);

    // Идентификация устройства
    zigbeeCold.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    zigbeeHot.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    // Configure sleep before starting the stack
    // Set threshold: device will enter deep sleep if idle time > 60 seconds
    esp_zb_sleep_set_threshold(DEEP_SLEEP_THRESHOLD);  
    esp_zb_sleep_enable(true);
    
    Serial.printf("Zigbee: Sleep enabled with %us threshold for deep sleep optimization\n", DEEP_SLEEP_THRESHOLD);

    // Start the stack
    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee: CRITICAL ERROR STARTING STACK");
        Serial.println("Data corruption detected or Partition Scheme mismatch.");
        Serial.println("Hold BOOT button during startup to Factory Reset.");
    }
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER);
}

// 2. Smart Zigbee Reporting
enum ReportState { 
    IDLE, 
    PENDING_COLD_CONFIG, PENDING_HOT_CONFIG, 
    PENDING_COLD_HOURLY, PENDING_HOT_HOURLY, 
    PENDING_COLD_VALUE,  PENDING_HOT_VALUE,
    PENDING_HEARTBEAT_COLD // A state specifically for the heartbeat sequence
};
static ReportState reportState = IDLE;
static uint32_t nextActionTime = 0;

void loop() {
    static bool connected_logged = false;
    static uint32_t last_loop_log = 0;
    static uint32_t last_sleep_cycle_start = 0;  // Track sleep cycle start
    uint32_t now = millis();
    
    updateSources();

    if (Zigbee.connected()) {
        if (!connected_logged) {
            Serial.println("Application: Zigbee.connected() is true. Main logic is now active.");
            connected_logged = true;
            last_sleep_cycle_start = now;
        }
        handleZigbeeReporting();
        handleAutoSave();
        handleConfigSave();
    } else {
        connected_logged = false;
    }

    updateStatusIndication();
    checkServiceButton();
    
    // Diagnostic logging with proper sleep cycle tracking
    if (now - last_loop_log >= 120000) {
        last_loop_log = now;
        uint32_t sleep_cycle_duration = now - last_sleep_cycle_start;
        // Reset cycle start for next measurement
        uint32_t prev_cycle_start = last_sleep_cycle_start;
        last_sleep_cycle_start = now;
        
        Serial.printf("System: Loop alive. Connected=%s, Uptime=%lu min, SleepCycleDuration=%lu ms\n", 
                      Zigbee.connected() ? "YES" : "NO", now / 60000, sleep_cycle_duration);
    }
    
    // Always delay to allow sleep, but more aggressively when idle
    if (reportState == IDLE && Zigbee.connected()) {
        delay(LOOP_IDLE_DELAY);  // 5000ms - deep sleep can trigger here
    } else {
        delay(100);  // Minimal delay during active reporting
    }
}

/* --- BLOCK IMPLEMENTATIONS --- */

// 1. Update Data Sources
void updateSources() {
    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();
}


void handleZigbeeReporting() {
    static uint32_t boot_time = millis();
    static uint32_t last_heartbeat = 0;
    static bool initial_config_sent = false;
    uint32_t now = millis();

    // Execute deferred action if it's time
    if (reportState != IDLE && now >= nextActionTime) {
        ReportState currentState = reportState;
        reportState = IDLE; // Reset state before execution
        switch (currentState) {
            case PENDING_COLD_CONFIG:
                zigbeeCold.reportConfig();
                reportState = PENDING_HOT_CONFIG;
                nextActionTime = now + 200;
                break;
            case PENDING_COLD_HOURLY:
                zigbeeCold.reportHourly();
                // Check if hot needs reporting (chaining)
                if (hotSrc && hotSrc->hasHourChanged()) {
                    reportState = PENDING_HOT_HOURLY;
                    nextActionTime = now + 100;
                }
                break;
            case PENDING_HEARTBEAT_COLD: // Heartbeat always reports both channels
                zigbeeCold.reportValue();
                reportState = PENDING_HOT_VALUE; // Unconditionally chain to hot report
                nextActionTime = now + 100;
                break;
            case PENDING_COLD_VALUE:
                zigbeeCold.reportValue();
                // This was an on-change report for cold.
                // Only chain the hot report if its value has also changed.
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
        return; // Executed one action per cycle
    }

    // Do not schedule new reports while there are pending ones
    if (reportState != IDLE) return;

    // One-time configuration report (SN and Offset) at startup, non-blocking
    if (!initial_config_sent && (now - boot_time > 5000)) {
        initial_config_sent = true;
        Serial.println("Zigbee: Reporting initial config...");
        reportState = PENDING_COLD_CONFIG;
        nextActionTime = now;
        return;
    }

    // Hourly consumption reports
    if (coldSrc && coldSrc->hasHourChanged()) {
        reportState = PENDING_COLD_HOURLY;
        nextActionTime = now;
    } else if (hotSrc && hotSrc->hasHourChanged()) {
        zigbeeHot.reportHourly(); // Only hot
    }

    // Total value reports (on change or heartbeat)
    bool coldNeeds = zigbeeCold.shouldReport();
    bool hotNeeds  = zigbeeHot.shouldReport();
    bool isHeartbeat = (now - last_heartbeat >= HEARTBEAT_INTERVAL);

    if (isHeartbeat) {
        Serial.println("Scheduling report -> Heartbeat");
        last_heartbeat = now;
        reportState = PENDING_HEARTBEAT_COLD; // Start the full heartbeat sequence
        nextActionTime = now;
        Utils::setLed(30, 30, 30);
    } else if (coldNeeds || hotNeeds) {
        // Serial.printf("Scheduling report -> On-change. Cold: %s, Hot: %s\n", coldNeeds ? "YES" : "no", hotNeeds ? "YES" : "no");
        // For on-change reports, schedule only what's needed
        if (coldNeeds) {
            reportState = PENDING_COLD_VALUE;
        } else { // This means only hotNeeds is true
            reportState = PENDING_HOT_VALUE;
        }
        nextActionTime = now;
        Utils::setLed(30, 30, 30);
    }

    // Battery report once an hour
    static uint32_t last_battery = 0;
    if (now - last_battery >= BATTERY_REPORT_INTERVAL || last_battery == 0) {
        last_battery = now;
        if (zigbeeCold.battery_supported()) zigbeeCold.reportBattery();
        if (zigbeeHot.battery_supported())  zigbeeHot.reportBattery();
    }
}

// 3. Auto-save (NVS)
void handleAutoSave() {
    static uint32_t last_save = 0;
    if (millis() - last_save >= 900000) { 
        last_save = millis();
        saveConfiguration(); 
    }
}

// 3.b. Сохранение конфигурации, если она была изменена через Zigbee
void handleConfigSave() {
    if (zigbeeCold.isConfigDirty() || zigbeeHot.isConfigDirty()) {
        saveConfiguration();
        zigbeeCold.clearConfigDirty();
        zigbeeHot.clearConfigDirty();
    }
}

// 4. Status LED
void updateStatusIndication() {
    static uint32_t last_blink = 0;
    if (!Zigbee.connected()) {
        if (millis() - last_blink > 500) {
            last_blink = millis();
            static bool t = false; t = !t;
            t ? Utils::setLed(20, 20, 0) : Utils::setLed(0, 0, 0);
        }
    } else {
        Utils::setLed(0, 1, 0); // Heartbeat LED
    }
}

// 5. Service Button
void checkServiceButton() {
    static uint32_t press_start = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (press_start == 0) {
            press_start = millis();
        } else if (millis() - press_start > 3000) {
            Utils::flashLed(50, 0, 0, 1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    } else {
        press_start = 0;
    }
}