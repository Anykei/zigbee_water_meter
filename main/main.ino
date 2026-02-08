/*
 * Copyright 2026 Andrey Nemenko
 *
 * Main application entry point for the ESP32-C6 Zigbee Water Meter.
 * This file handles hardware initialization, Zigbee stack configuration,
 * and the main event loop using a non-blocking architecture.
 */

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_pm.h"
#include <Preferences.h>

// Abstraction Layers
#include "utils.h"
#include "zigbee_water_meter.h"
#include "hwi_streams/rs485_stream.h"
#include "drivers/driver_factory.h"
#include "sources/factory_source.h"

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

/* --- ZIGBEE CONFIGURATION --- */
#define MODEL_ID "C6_WATER_METER"
#define MANUFACTURER_NAME "MuseLab"
#define TX_POWER 12

/* --- APPLICATION CONFIGURATION --- */
// For testing, use Source::SourceType::Test or Driver::MeterModel::Mock
// constexpr Source::SourceType COLD_TYPE = Source::SourceType::Test;
// constexpr Source::SourceType HOT_TYPE  = Source::SourceType::Test;
// constexpr uint32_t COLD_POOL_INTERVAL = 3000; // Интервал опроса для холодного канала (мс) Test
// constexpr uint32_t HOT_POOL_INTERVAL  = 3000; // Интервал опроса для горячего канала (мс) Test

constexpr uint32_t HEARTBEAT_INTERVAL = 60000 * 60; // Heartbeat interval for HA (ms)

constexpr uint32_t COLD_POOL_INTERVAL = 60000 * 5; // Polling interval for cold channel (ms)
constexpr uint32_t HOT_POOL_INTERVAL  = 60000 * 5; // Polling interval for hot channel (ms)

constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;

constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;

constexpr bool NEED_RS485 = (COLD_TYPE == Source::SourceType::Smart || HOT_TYPE == Source::SourceType::Smart);

/* --- GLOBAL OBJECTS --- */
Preferences prefs;
RS485Stream* rs485Bus = nullptr; 

// Zigbee Endpoints
ZigbeeWaterMeter zigbeeCold(1, true); 
ZigbeeWaterMeter zigbeeHot(2, false);

// Interfaces (Pointers)
Driver::SmartMeterDriver *coldDrv = nullptr;
Driver::SmartMeterDriver *hotDrv = nullptr;
Source::WaterSource *coldSrc = nullptr;
Source::WaterSource *hotSrc = nullptr;

/* --- ZIGBEE EVENT HANDLER --- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    if (callback_id == ESP_ZB_CORE_APP_SIGNAL_CB_ID) {
        auto *signal = (esp_zb_app_signal_t *)message;
        esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*signal->p_app_signal;
        if (sig_type == ESP_ZB_ZDO_SIGNAL_LEAVE) {
            Serial.println("Zigbee: Connection lost (Leave).");
            Utils::flashLed(50, 0, 0, 500); // Red flash warning
            // Manual steering removed to avoid loops. Relying on setRebootOnLeave(true).
            if constexpr (NEED_RS485) digitalWrite(RS485_POWER_PIN, LOW);
        }

        if (sig_type == ESP_ZB_BDB_SIGNAL_STEERING) {
            esp_err_t *status = (esp_err_t *)esp_zb_app_signal_get_params(signal->p_app_signal);
            if (*status == ESP_OK) {
                Serial.println("Zigbee: Connected. Enabling RS485 power.");
                if constexpr (NEED_RS485) digitalWrite(RS485_POWER_PIN, HIGH);
            }
        }
    }

    if (callback_id == ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        Serial.println("Zigbee: Entering sleep mode...");
        Serial.flush(); // Wait for message to be sent before sleeping
        
        if constexpr (NEED_RS485) {
            Serial1.end(); 
            digitalWrite(RS485_POWER_PIN, LOW);
        }
        
        esp_zb_sleep_now();
        
        if constexpr (NEED_RS485) {
            digitalWrite(RS485_POWER_PIN, HIGH);
            Serial1.begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
        }
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        auto *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        Utils::setLed(0, 30, 30); // Cyan flash on command from HA
        for (auto ep : Zigbee.ep_objects) {
            if (msg->info.dst_endpoint == ep->getEndpoint()) {
                static_cast<ZigbeeWaterMeter*>(ep)->handleAttributeWrite(msg);
                break;
            }
        }
    }
    return ESP_OK;
}

/* --- INTERRUPTS (PulseSource Only) --- */
void IRAM_ATTR isr_cold() { 
    if(coldSrc && COLD_TYPE != Source::SourceType::Smart) {
        static_cast<Source::PulseSource*>(coldSrc)->increment(); 
    }
}

void IRAM_ATTR isr_hot() { 
    if(hotSrc && HOT_TYPE != Source::SourceType::Smart) {
        static_cast<Source::PulseSource*>(hotSrc)->increment(); 
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

// Standard Arduino setup function.
void setup() {
    initHardware();    // Layer 0: Hardware and Power
    loadSystemData();  // Layer 1: Storage (NVS)
    initSources();     // Layer 2: Drivers and Sources
    setupZigbee();     // Layer 3: Network Stack
    
    Serial.println("--- System initialized and running ---");
    Utils::flashLed(0, 30, 0, 1000); // Final green signal
}

void initHardware() {
    Serial.begin(115200);
    Utils::setLed(30, 0, 0); // Статус: Загрузка
    
    // Питание и сон
    esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true };
    esp_pm_configure(&pm_config);

    // Шина данных
    if constexpr (NEED_RS485) {
        pinMode(RS485_POWER_PIN, OUTPUT);
        digitalWrite(RS485_POWER_PIN, HIGH); // Включаем питание шины

        rs485Bus = new RS485Stream(&Serial1, RS485_EN);
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
        coldDrv = Driver::DriverFactory::create(COLD_DRV_MODEL, rs485Bus, c_sn);
        if (coldDrv) coldDrv->setLogger(&Serial);
    } else {
        Serial.println("Cold driver not created");
    }

    if constexpr (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv = Driver::DriverFactory::create(HOT_DRV_MODEL, rs485Bus, h_sn);
        if (hotDrv) hotDrv->setLogger(&Serial);
    } else {
        Serial.println("Hot driver not created");
    }

    // 3. Create Sources (Logic Layer)
    coldSrc = Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv);
    hotSrc  = Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv);

    // 4. Fine Tuning (Offsets & Start)
    if (coldSrc) { 
        coldSrc->setPollInterval(COLD_POOL_INTERVAL);
        coldSrc->setOffset(c_off); 
        coldSrc->setSerialNumber(c_sn);
        coldSrc->begin(); 
    } else {
        Serial.println("Cold source not created");
    }
    if (hotSrc) { 
        hotSrc->setPollInterval(HOT_POOL_INTERVAL);
        hotSrc->setOffset(h_off); 
        hotSrc->setSerialNumber(h_sn);
        hotSrc->begin(); 
    } else {
        Serial.println("Hot source not created");   
    }
}

void setupZigbee() {
    // Bind sources to endpoints
    zigbeeCold.setSource(coldSrc);
    zigbeeHot.setSource(hotSrc);

    // Configure save callbacks
    zigbeeCold.onSettingsChanged(saveConfiguration);
    zigbeeHot.onSettingsChanged(saveConfiguration);

    // Register endpoints in the stack
    zigbeeCold.begin(); 
    zigbeeHot.begin();
    Zigbee.addEndpoint(&zigbeeCold); 
    Zigbee.addEndpoint(&zigbeeHot);

    // Идентификация устройства
    zigbeeCold.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    zigbeeHot.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    // Start the stack
    Zigbee.setRebootOnLeave(true);
    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee: CRITICAL ERROR STARTING STACK");
    }

    // Final stack configuration
    esp_zb_sleep_enable(true);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER);
}

void loop() {
    // 1. Poll Hardware (RS-485 / Pulse)
    static bool connected_logged = false;
    updateSources();

    // 2. Network Logic (only if connected)
    if (Zigbee.connected()) {
        if (!connected_logged) {
            Serial.println("Application: Zigbee.connected() is true. Main logic is now active.");
            connected_logged = true;
        }
        handleZigbeeReporting();
        handleAutoSave();
    }

    // 3. UI and Service
    updateStatusIndication();
    checkServiceButton();
}

/* --- BLOCK IMPLEMENTATIONS --- */

// 1. Update Data Sources
void updateSources() {
    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();
}

// 2. Smart Zigbee Reporting
enum ReportState { 
    IDLE, 
    PENDING_COLD_CONFIG, PENDING_HOT_CONFIG, 
    PENDING_COLD_HOURLY, PENDING_HOT_HOURLY, 
    PENDING_COLD_VALUE,  PENDING_HOT_VALUE 
};
static ReportState reportState = IDLE;
static uint32_t nextActionTime = 0;

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
            case PENDING_COLD_VALUE:
                zigbeeCold.reportValue();
                // Check if hot needs reporting (chaining)
                // We cannot use local variables from loop here,
                // so we rely on re-checking shouldReport() or timeout.
                // Since timeout is shared, it's easier to check shouldReport() for Hot.
                if (zigbeeHot.shouldReport() || (now - last_heartbeat < 1000)) { // < 1000 means it was a heartbeat
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
    bool timeout = (now - last_heartbeat >= HEARTBEAT_INTERVAL);
    if (coldNeeds || hotNeeds || timeout) {
        last_heartbeat = now;
        
        if (coldNeeds || timeout) {
            reportState = PENDING_COLD_VALUE;
            nextActionTime = now;
        } else if (hotNeeds) { // Only hot channel needs reporting
            zigbeeHot.reportValue();
        }

        if (coldNeeds || hotNeeds) Utils::setLed(30, 30, 30);
    }

    // Battery report once an hour
    static uint32_t last_battery = 0;
    if (now - last_battery >= 3600000 || last_battery == 0) {
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