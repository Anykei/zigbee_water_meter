// Copyright 2026 Andrey Nemenko.
//
// Main application entry point for the ESP32-C6 Zigbee Water Meter. This file
// wires hardware initialization, persisted state, sources, and Zigbee reports.

#ifdef RS485_UART_BRIDGE

#include <Arduino.h>

#ifndef RS485_RX
#define RS485_RX 21
#endif
#ifndef RS485_TX
#define RS485_TX 20
#endif
#ifndef RS485_EN
#define RS485_EN -1
#endif
#ifndef RS485_BAUD
#define RS485_BAUD 9600
#endif
#ifndef RS485_POWER_PIN
#define RS485_POWER_PIN -1
#endif
#ifndef RS485_POWER_CONTROL_ENABLED
#define RS485_POWER_CONTROL_ENABLED 0
#endif
#ifndef RS485_POWER_ACTIVE_LOW
#define RS485_POWER_ACTIVE_LOW 0
#endif
#ifndef RS485_POWER_SETTLE_MS
#define RS485_POWER_SETTLE_MS 100
#endif
#ifndef RS485_BRIDGE_USB_BAUD
#define RS485_BRIDGE_USB_BAUD 9600
#endif
#ifndef RS485_BRIDGE_BUFFER_SIZE
#define RS485_BRIDGE_BUFFER_SIZE 128
#endif
#ifndef RS485_BRIDGE_FRAME_GAP_US
#define RS485_BRIDGE_FRAME_GAP_US 3000
#endif

#define RS485_CONFIG SERIAL_8N1

constexpr bool kBridgeHasPowerControl = RS485_POWER_CONTROL_ENABLED && (RS485_POWER_PIN >= 0);
constexpr bool kBridgeHasDirectionPin = RS485_EN >= 0;

void setBridgePower(bool on) {
    if constexpr (!kBridgeHasPowerControl) return;

    const bool level = RS485_POWER_ACTIVE_LOW ? !on : on;
    digitalWrite(RS485_POWER_PIN, level ? HIGH : LOW);
}

void setBridgeTransmit(bool on) {
    if constexpr (!kBridgeHasDirectionPin) return;

    digitalWrite(RS485_EN, on ? HIGH : LOW);
}

void writeRs485(const uint8_t *buffer, size_t size) {
    if (size == 0) return;

    setBridgeTransmit(true);
    Serial1.write(buffer, size);
    Serial1.flush();
    setBridgeTransmit(false);
}

void setup() {
    Serial.begin(RS485_BRIDGE_USB_BAUD);

    if constexpr (kBridgeHasPowerControl) {
        pinMode(RS485_POWER_PIN, OUTPUT);
        setBridgePower(true);
        delay(RS485_POWER_SETTLE_MS);
    }

    if constexpr (kBridgeHasDirectionPin) {
        pinMode(RS485_EN, OUTPUT);
        setBridgeTransmit(false);
    }

    Serial1.begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
}

void pumpUsbToRs485() {
    static uint8_t buffer[RS485_BRIDGE_BUFFER_SIZE];
    static size_t size = 0;
    static uint32_t lastByteAt = 0;

    while (Serial.available() > 0) {
        if (size >= sizeof(buffer)) {
            writeRs485(buffer, size);
            size = 0;
        }
        buffer[size++] = static_cast<uint8_t>(Serial.read());
        lastByteAt = micros();
    }

    if (size > 0 && static_cast<uint32_t>(micros() - lastByteAt) >= RS485_BRIDGE_FRAME_GAP_US) {
        writeRs485(buffer, size);
        size = 0;
    }
}

void pumpRs485ToUsb() {
    static uint8_t buffer[RS485_BRIDGE_BUFFER_SIZE];
    static size_t size = 0;
    static uint32_t lastByteAt = 0;

    while (Serial1.available() > 0) {
        if (size >= sizeof(buffer)) {
            Serial.write(buffer, size);
            size = 0;
        }
        buffer[size++] = static_cast<uint8_t>(Serial1.read());
        lastByteAt = micros();
    }

    if (size > 0 && static_cast<uint32_t>(micros() - lastByteAt) >= RS485_BRIDGE_FRAME_GAP_US) {
        Serial.write(buffer, size);
        Serial.flush();
        size = 0;
    }
}

void loop() {
    pumpUsbToRs485();
    pumpRs485ToUsb();
}

#else

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>
#include "nvs_flash.h"
#include "esp_partition.h"
#include <algorithm>
#include <memory>

#include "utils.h"
#include "zigbee_water_meter.h"
#include "zigbee_device_power.h"
#include "hwi_streams/rs485_stream.h"
#include "drivers/driver_factory.h"
#include "sources/factory_source.h"

#include "../include/version.h"

// Hardware settings come from PlatformIO build flags. The fallback values keep
// local builds explicit when a board environment omits a define.
#ifndef RGB_LED_PIN
#define RGB_LED_PIN -1
#endif
#ifndef GPIO_STATUS_LED_PIN
  #if defined(LED_BUILTIN)
    #define GPIO_STATUS_LED_PIN LED_BUILTIN
  #else
    #define GPIO_STATUS_LED_PIN -1
  #endif
#endif
#ifndef GPIO_STATUS_LED_ACTIVE_LOW
#define GPIO_STATUS_LED_ACTIVE_LOW 0
#endif
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN  9
#endif
#ifndef RS485_POWER_PIN
#define RS485_POWER_PIN  18
#endif
#ifndef RS485_POWER_CONTROL_ENABLED
#define RS485_POWER_CONTROL_ENABLED 1
#endif
#ifndef RS485_RX
#define RS485_RX         21
#endif
#ifndef RS485_TX
#define RS485_TX         20
#endif
#ifndef RS485_EN
#define RS485_EN         19
#endif
#ifndef RS485_BAUD
#define RS485_BAUD       9600
#endif
#define RS485_CONFIG     SERIAL_8N1
#ifndef PULSE_COLD_PIN
#define PULSE_COLD_PIN   10
#endif
#ifndef PULSE_HOT_PIN
#define PULSE_HOT_PIN    11
#endif

#ifndef ADC_BATTERY_VOLTAGE_PIN
#define ADC_BATTERY_VOLTAGE_PIN 1
#endif
#ifndef ADC_BATTERY_VOLTAGE_HI
#define ADC_BATTERY_VOLTAGE_HI 3.3
#endif
#ifndef ADC_BATTERY_VOLTAGE_LO
#define ADC_BATTERY_VOLTAGE_LO 2.0
#endif
#ifndef ADC_BATTERY_VOLTAGE_SCALE
#define ADC_BATTERY_VOLTAGE_SCALE 2.0
#endif
#ifndef ADC_BATTERY_VOLTAGE_OFFSET_MV
#define ADC_BATTERY_VOLTAGE_OFFSET_MV 0
#endif
#ifndef ADC_BATTERY_ENABLE_PIN
#define ADC_BATTERY_ENABLE_PIN -1
#endif
#ifndef ADC_BATTERY_ENABLE_ACTIVE_LOW
#define ADC_BATTERY_ENABLE_ACTIVE_LOW 0
#endif
#ifndef ADC_BATTERY_POWER_SETTLE_MS
#define ADC_BATTERY_POWER_SETTLE_MS 20
#endif
#ifndef ADC_BATTERY_SAMPLES
#define ADC_BATTERY_SAMPLES 12
#endif
#ifndef ADC_BATTERY_SAMPLE_DELAY_MS
#define ADC_BATTERY_SAMPLE_DELAY_MS 2
#endif
#ifndef ADC_BATTERY_PERCENT_SMOOTHING_SAMPLES
#define ADC_BATTERY_PERCENT_SMOOTHING_SAMPLES 5
#endif
#ifndef ADC_BATTERY_CURVE_EMPTY_MV
#define ADC_BATTERY_CURVE_EMPTY_MV 2000
#endif
#ifndef ADC_BATTERY_CURVE_LOW_MV
#define ADC_BATTERY_CURVE_LOW_MV 2350
#endif
#ifndef ADC_BATTERY_CURVE_MID_MV
#define ADC_BATTERY_CURVE_MID_MV 2700
#endif
#ifndef ADC_BATTERY_CURVE_HIGH_MV
#define ADC_BATTERY_CURVE_HIGH_MV 3050
#endif
#ifndef ADC_BATTERY_CURVE_FULL_MV
#define ADC_BATTERY_CURVE_FULL_MV 3300
#endif
#ifndef LOW_BATTERY_CRITICAL_PERCENT
#define LOW_BATTERY_CRITICAL_PERCENT 5
#endif
#ifndef RS485_POWER_SETTLE_MS
#define RS485_POWER_SETTLE_MS 100
#endif
#ifndef RS485_POWER_ACTIVE_LOW
#define RS485_POWER_ACTIVE_LOW 0
#endif
#ifndef RS485_POWER_IDLE_OFF_MS
#define RS485_POWER_IDLE_OFF_MS 60000
#endif
#ifndef RS485_TX_TEST_MS
#define RS485_TX_TEST_MS 0
#endif
#ifndef RS485_TX_GPIO_TEST_MS
#define RS485_TX_GPIO_TEST_MS 0
#endif
#ifndef DEFAULT_COLD_SERIAL
#define DEFAULT_COLD_SERIAL 0
#endif
#ifndef DEFAULT_HOT_SERIAL
#define DEFAULT_HOT_SERIAL 0
#endif

// Zigbee identity settings can be overridden per PlatformIO environment.
#ifndef MODEL_ID
#define MODEL_ID "C6_WATER_METER"
#endif
#ifndef MANUFACTURER_NAME
#define MANUFACTURER_NAME "MuseLab"
#endif
#ifndef TX_POWER
#define TX_POWER 20
#endif
#define RECCONNECT_TIMEOUT 60000

namespace TimeLiterals {
constexpr uint32_t operator"" _ms(unsigned long long value) {
    return static_cast<uint32_t>(value);
}

constexpr uint32_t operator"" _s(unsigned long long value) {
    return static_cast<uint32_t>(value * 1000ULL);
}

constexpr uint32_t operator"" _mins(unsigned long long value) {
    return static_cast<uint32_t>(value * 60ULL * 1000ULL);
}

constexpr uint32_t operator"" _h(unsigned long long value) {
    return static_cast<uint32_t>(value * 60ULL * 60ULL * 1000ULL);
}

constexpr uint32_t operator"" _s_sec(unsigned long long value) {
    return static_cast<uint32_t>(value);
}

constexpr uint32_t operator"" _mins_sec(unsigned long long value) {
    return static_cast<uint32_t>(value * 60ULL);
}

constexpr uint32_t operator"" _h_sec(unsigned long long value) {
    return static_cast<uint32_t>(value * 60ULL * 60ULL);
}
}  // namespace TimeLiterals

using namespace TimeLiterals;

// Keep this false in normal builds. When enabled, hourly and daily accounting
// windows are shortened for telemetry testing.
constexpr bool kEnableTestIntervals = false;

// Keep the device awake long enough for Zigbee interview and configuration.
constexpr uint32_t COMMISSIONING_AWAKE_MS = 3_mins;

#ifdef TEST
#ifdef TEST_REAL_METERS
constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE  = Source::SourceType::Smart;
#else
constexpr Source::SourceType COLD_TYPE = Source::SourceType::Test;
constexpr Source::SourceType HOT_TYPE  = Source::SourceType::Test;
#endif
constexpr uint32_t COLD_POOL_INTERVAL = 3_s;
constexpr uint32_t HOT_POOL_INTERVAL  = 3_s;

constexpr uint32_t HEARTBEAT_INTERVAL = 1_mins;
constexpr uint32_t BATTERY_REPORT_INTERVAL = 1_mins;
constexpr uint32_t DEEP_SLEEP_THRESHOLD_SECONDS = 1_h_sec;
constexpr uint32_t LOOP_IDLE_DELAY = 100_ms;

#else
constexpr uint32_t HEARTBEAT_INTERVAL = 30_mins;
constexpr uint32_t BATTERY_REPORT_INTERVAL = 6_h;
constexpr uint32_t COLD_POOL_INTERVAL = 30_mins;
constexpr uint32_t HOT_POOL_INTERVAL  = 30_mins;
constexpr uint32_t DEEP_SLEEP_THRESHOLD_SECONDS = 1_mins_sec;
constexpr uint32_t LOOP_IDLE_DELAY = 15_s;

constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;
#endif


constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;

// Compile-time flag avoids initializing RS485 hardware in Pulse/Test-only builds.
constexpr bool NEED_RS485 = (COLD_TYPE == Source::SourceType::Smart || HOT_TYPE == Source::SourceType::Smart);
constexpr bool COLD_USES_RS485 = COLD_TYPE == Source::SourceType::Smart;
constexpr bool HOT_USES_RS485 = HOT_TYPE == Source::SourceType::Smart;
constexpr bool HAS_RS485_POWER_CONTROL = NEED_RS485 && RS485_POWER_CONTROL_ENABLED && (RS485_POWER_PIN >= 0);

// Runtime sleep is deferred until the coordinator has had time to interview all
// endpoints and apply configuration.
static uint32_t g_join_time_ms = 0;
static bool g_sleep_enabled_runtime = false;

inline void StatusLedSet(uint8_t r, uint8_t g, uint8_t b) {
    Utils::setLed(r, g, b);
}

inline void StatusLedFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    Utils::flashLed(r, g, b, ms);
}

Preferences prefs;
std::unique_ptr<RS485Stream> rs485Bus = nullptr; 

// Endpoint layout is part of the Zigbee2MQTT converter contract.
ZigbeeWaterMeter zigbeeCold(1, true);
ZigbeeWaterMeter zigbeeHot(2, false);
ZigbeeDevicePower zigbeePower(
    3,
    ADC_BATTERY_VOLTAGE_PIN,
    ADC_BATTERY_VOLTAGE_LO,
    ADC_BATTERY_VOLTAGE_HI,
    ADC_BATTERY_VOLTAGE_SCALE,
    ADC_BATTERY_VOLTAGE_OFFSET_MV,
    ADC_BATTERY_ENABLE_PIN,
    ADC_BATTERY_ENABLE_ACTIVE_LOW,
    ADC_BATTERY_POWER_SETTLE_MS,
    ADC_BATTERY_SAMPLES,
    ADC_BATTERY_SAMPLE_DELAY_MS,
    ADC_BATTERY_CURVE_EMPTY_MV,
    ADC_BATTERY_CURVE_LOW_MV,
    ADC_BATTERY_CURVE_MID_MV,
    ADC_BATTERY_CURVE_HIGH_MV,
    ADC_BATTERY_CURVE_FULL_MV,
    ADC_BATTERY_PERCENT_SMOOTHING_SAMPLES
);


// Drivers own protocol state; sources own per-channel metering state.
std::unique_ptr<Driver::SmartMeterDriver> coldDrv = nullptr;
std::unique_ptr<Driver::SmartMeterDriver> hotDrv = nullptr;
std::unique_ptr<Source::WaterSource> coldSrc = nullptr;
std::unique_ptr<Source::WaterSource> hotSrc = nullptr;

static bool g_rs485_power_on = false;
static bool g_rs485_bus_started = false;

void setRs485Power(bool on) {
    if constexpr (!HAS_RS485_POWER_CONTROL) return;

    if (g_rs485_power_on == on) return;
    const bool level = RS485_POWER_ACTIVE_LOW ? !on : on;
    digitalWrite(RS485_POWER_PIN, level ? HIGH : LOW);
    g_rs485_power_on = on;
    Serial.printf("RS485: power %s on GPIO%d\n", on ? "ON" : "OFF", RS485_POWER_PIN);
}

void beginRs485Bus() {
    if (!rs485Bus || g_rs485_bus_started) return;

    rs485Bus->begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
    rs485Bus->setTimeout(300);
    g_rs485_bus_started = true;
}

void endRs485Bus() {
    if (!rs485Bus || !g_rs485_bus_started) return;

    rs485Bus->flush();
    rs485Bus->end();
    g_rs485_bus_started = false;

    if constexpr (RS485_TX >= 0) pinMode(RS485_TX, INPUT);
    if constexpr (RS485_RX >= 0) pinMode(RS485_RX, INPUT);
    if constexpr (RS485_EN >= 0) pinMode(RS485_EN, INPUT);
}

void enableRs485ForPoll() {
    if constexpr (HAS_RS485_POWER_CONTROL) {
        const bool wasOn = g_rs485_power_on;
        setRs485Power(true);
        if (!wasOn) delay(RS485_POWER_SETTLE_MS);
    }
    beginRs485Bus();
}

void disableRs485AfterPoll() {
    if constexpr (!HAS_RS485_POWER_CONTROL) return;

    endRs485Bus();
    setRs485Power(false);
}

uint32_t millisUntilNextRs485Poll(uint32_t now) {
    uint32_t nextDelay = UINT32_MAX;

    if constexpr (COLD_USES_RS485) {
        if (coldSrc) nextDelay = std::min(nextDelay, coldSrc->millisUntilPoll(now));
    }
    if constexpr (HOT_USES_RS485) {
        if (hotSrc) nextDelay = std::min(nextDelay, hotSrc->millisUntilPoll(now));
    }

    return nextDelay;
}

bool shouldPowerDownRs485(uint32_t now) {
    if constexpr (!HAS_RS485_POWER_CONTROL) return false;
    if constexpr (RS485_POWER_IDLE_OFF_MS == 0) return true;

    const uint32_t nextDelay = millisUntilNextRs485Poll(now);
    return nextDelay == UINT32_MAX || nextDelay > RS485_POWER_IDLE_OFF_MS;
}

// Handles Zigbee stack callbacks that affect attributes, connection state, or
// sleep scheduling.
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    if (message == nullptr) {
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        auto *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        if (msg->info.dst_endpoint == zigbeeCold.getEndpoint()) {
            StatusLedSet(0, 30, 30);
            zigbeeCold.handleAttributeWrite(msg);
        } else if (msg->info.dst_endpoint == zigbeeHot.getEndpoint()) {
            StatusLedSet(0, 30, 30);
            zigbeeHot.handleAttributeWrite(msg);
        }
        return ESP_OK;
    }

    // End devices let the Zigbee stack enter light sleep automatically. Calling
    // esp_zb_sleep_now() here can race with stack-owned scheduling.
    if (callback_id == (esp_zb_core_action_callback_id_t)ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        return ESP_OK;
    }

    // The remaining handled callbacks use esp_zb_app_signal_t payloads.
    esp_zb_app_signal_t *signal = (esp_zb_app_signal_t *)message;
    if (signal->p_app_signal == nullptr) {
        return ESP_OK;
    }

    esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*signal->p_app_signal;
    esp_err_t sig_status = signal->esp_err_status;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_LEAVE:
            Serial.println("Zigbee: Connection lost (Leave). Rebooting...");
            StatusLedFlash(50, 0, 0, 500);
            disableRs485AfterPoll();
            delay(100);
            esp_restart();
            break;

        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (sig_status == ESP_OK) {
                Serial.println("Zigbee: Connected successfully.");
                g_join_time_ms = millis();
                g_sleep_enabled_runtime = false;
                esp_zb_sleep_enable(false);
                Serial.println("Zigbee: Sleep temporarily disabled for interview window.");
            } else {
                Serial.printf("Zigbee: Steering failed with status 0x%x\n", sig_status);
            }
            break;

        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            Serial.println("Zigbee: Device already commissioned, skipping pairing.");
            g_join_time_ms = millis();
            g_sleep_enabled_runtime = false;
            esp_zb_sleep_enable(false);
            Serial.println("Zigbee: Sleep temporarily disabled after startup.");
            break;

        default:
            break;
    }
    
    return ESP_OK;
}

// These ISRs are only attached when the matching source is a PulseSource.
void IRAM_ATTR isr_cold() { 
    if(coldSrc) {
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

// Erases NVS and Zigbee storage only when the boot button is held through the
// confirmation delay.
void checkBootRecovery() {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        Serial.println("\n!!! BOOT BUTTON HELD - RECOVERY MODE !!!");
        StatusLedSet(50, 0, 0);
        delay(3000);
        
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

            StatusLedFlash(0, 50, 0, 1000);
            Serial.println("Restarting...");
            ESP.restart();
        }
    }
}

// PlatformIO compiles this file as C++, so declarations keep Arduino-style
// function ordering explicit.
void initHardware();
void checkBootRecovery();
void loadSystemData();
void initSources();
void setupZigbee();
void updateSources();
void handleZigbeeReporting();
void handleAutoSave();
void handleConfigSave();
void updateStatusIndication();
void checkServiceButton();
void saveConfiguration();
#ifdef TEST
void logBatteryAdcForTest();
#endif

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
    
    initHardware();
    checkBootRecovery();
    loadSystemData();
    initSources();
    setupZigbee();
    
    Serial.println("--- System initialized and running ---");
    StatusLedFlash(0, 30, 0, 1000);
}

void initHardware() {
    Serial.begin(115200);
    StatusLedSet(30, 0, 0);

    if constexpr (NEED_RS485) {
        rs485Bus = std::make_unique<RS485Stream>(&Serial1, RS485_EN);

        if constexpr (HAS_RS485_POWER_CONTROL) {
            pinMode(RS485_POWER_PIN, OUTPUT);
            const bool offLevel = RS485_POWER_ACTIVE_LOW ? true : false;
            digitalWrite(RS485_POWER_PIN, offLevel ? HIGH : LOW);
            g_rs485_power_on = false;
            endRs485Bus();
        }

#if RS485_TX_GPIO_TEST_MS > 0
        enableRs485ForPoll();
        Serial.printf("RS485: GPIO TX pin test on GPIO%d for %lu ms\n", RS485_TX, (unsigned long)RS485_TX_GPIO_TEST_MS);
        pinMode(RS485_TX, OUTPUT);
        const uint32_t gpioTestStart = millis();
        bool level = false;
        while (millis() - gpioTestStart < RS485_TX_GPIO_TEST_MS) {
            level = !level;
            digitalWrite(RS485_TX, level ? HIGH : LOW);
            delay(500);
        }
        digitalWrite(RS485_TX, HIGH);
#endif

        if constexpr (!HAS_RS485_POWER_CONTROL) {
            beginRs485Bus();
        }

        Serial.printf(
            "RS485: UART RX=%d TX=%d EN=%d power=%d power_control=%d active_low=%d idle_off=%lu ms baud=%lu\n",
            RS485_RX,
            RS485_TX,
            RS485_EN,
            RS485_POWER_PIN,
            RS485_POWER_CONTROL_ENABLED,
            RS485_POWER_ACTIVE_LOW,
            (unsigned long)RS485_POWER_IDLE_OFF_MS,
            (unsigned long)RS485_BAUD
        );

#if RS485_TX_TEST_MS > 0
        enableRs485ForPoll();
        Serial.printf("RS485: TX diagnostic burst for %lu ms\n", (unsigned long)RS485_TX_TEST_MS);
        const uint32_t txTestStart = millis();
        while (millis() - txTestStart < RS485_TX_TEST_MS) {
            rs485Bus->write((uint8_t)0x55);
            delay(2);
        }
        rs485Bus->flush();
#endif

#if RS485_TX_GPIO_TEST_MS > 0 || RS485_TX_TEST_MS > 0
        disableRs485AfterPoll();
#endif
    }
    
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
}

void loadSystemData() {
    prefs.begin("water", false);
}

void initSources() {
    // Storage keys are intentionally short to reduce NVS metadata overhead.
    uint32_t c_sn  = prefs.getUInt("sc", 0);
    uint32_t h_sn  = prefs.getUInt("sh", 0);
    uint64_t c_lit = prefs.getULong64("cl", 0);
    uint64_t h_lit = prefs.getULong64("hl", 0);
    int32_t  c_off = prefs.getInt("oc", 0);
    int32_t  h_off = prefs.getInt("oh", 0);
    uint32_t c_poll_min = prefs.getUInt("pc", 0);
    uint32_t h_poll_min = prefs.getUInt("ph", 0);

#if DEFAULT_COLD_SERIAL > 0
    if (c_sn == 0) {
        c_sn = DEFAULT_COLD_SERIAL;
        Serial.printf("Using build default cold serial: %lu\n", (unsigned long)c_sn);
    }
#endif
#if DEFAULT_HOT_SERIAL > 0
    if (h_sn == 0) {
        h_sn = DEFAULT_HOT_SERIAL;
        Serial.printf("Using build default hot serial: %lu\n", (unsigned long)h_sn);
    }
#endif

    Serial.printf(
        "Loaded config -> Cold SN:%lu, Cold Off:%ld, Cold Poll:%lu min, Hot SN:%lu, Hot Off:%ld, Hot Poll:%lu min\n",
        c_sn,
        c_off,
        (unsigned long)(c_poll_min > 0 ? c_poll_min : (COLD_POOL_INTERVAL + Source::kMsPerMinute - 1) / Source::kMsPerMinute),
        h_sn,
        h_off,
        (unsigned long)(h_poll_min > 0 ? h_poll_min : (HOT_POOL_INTERVAL + Source::kMsPerMinute - 1) / Source::kMsPerMinute)
    );

    if constexpr (COLD_TYPE == Source::SourceType::Smart) {
        coldDrv.reset(Driver::DriverFactory::create(COLD_DRV_MODEL, rs485Bus.get(), c_sn));
        if (coldDrv) coldDrv->setLogger(&Serial);
    } else {
        Serial.println("Cold driver not created");
    }

    if constexpr (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv.reset(Driver::DriverFactory::create(HOT_DRV_MODEL, rs485Bus.get(), h_sn));
        if (hotDrv) hotDrv->setLogger(&Serial);
    } else {
        Serial.println("Hot driver not created");
    }

    coldSrc.reset(Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv.get()));
    hotSrc.reset(Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv.get()));

    if (coldSrc) { 
        if (c_poll_min > 0) {
            coldSrc->setPollIntervalMinutes(c_poll_min);
        } else {
            coldSrc->setPollInterval(COLD_POOL_INTERVAL);
        }
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
        if (h_poll_min > 0) {
            hotSrc->setPollIntervalMinutes(h_poll_min);
        } else {
            hotSrc->setPollInterval(HOT_POOL_INTERVAL);
        }
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

    esp_zb_sleep_set_threshold(DEEP_SLEEP_THRESHOLD_SECONDS);
    esp_zb_sleep_enable(false);
    g_sleep_enabled_runtime = false;
    
    Serial.printf("Zigbee: Sleep deferred. Threshold=%lus\n", (unsigned long)DEEP_SLEEP_THRESHOLD_SECONDS);

    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee: CRITICAL ERROR STARTING STACK");
        Serial.println("Data corruption detected or Partition Scheme mismatch.");
        Serial.println("Hold BOOT button during startup to Factory Reset.");
    }
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER);
}

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

void loop() {
    static bool connected_logged = false;
    static uint32_t last_loop_log = 0;
    static uint32_t last_sleep_cycle_start = 0;
    uint32_t now = millis();
    
    updateSources();
#ifdef TEST
    logBatteryAdcForTest();
#endif

    if (Zigbee.connected()) {
        if (!connected_logged) {
            Serial.println("Application: Zigbee.connected() is true. Main logic is now active.");
            connected_logged = true;
            last_sleep_cycle_start = now;
            if (g_join_time_ms == 0) g_join_time_ms = now;
        }

        if (!g_sleep_enabled_runtime && g_join_time_ms != 0 && (now - g_join_time_ms) >= COMMISSIONING_AWAKE_MS) {
            esp_zb_sleep_enable(true);
            g_sleep_enabled_runtime = true;
            Serial.printf("Zigbee: Sleep enabled after %lu ms commissioning window.\n", (unsigned long)COMMISSIONING_AWAKE_MS);
        }
        handleZigbeeReporting();
        handleAutoSave();
        handleConfigSave();
    } else {
        connected_logged = false;
    }

    updateStatusIndication();
    checkServiceButton();
    
    if (now - last_loop_log >= 120000) {
        last_loop_log = now;
        uint32_t sleep_cycle_duration = now - last_sleep_cycle_start;
        last_sleep_cycle_start = now;
        
        Serial.printf("System: Loop alive. Connected=%s, Uptime=%lu min, SleepCycleDuration=%lu ms\n", 
                      Zigbee.connected() ? "YES" : "NO", now / 60000, sleep_cycle_duration);
    }
    
    if (reportState == IDLE && Zigbee.connected()) {
        delay(LOOP_IDLE_DELAY);
    } else {
        delay(100);
    }
}

#ifdef TEST
// Logs battery ADC input without requiring Zigbee connectivity.
void logBatteryAdcForTest() {
    static uint32_t last_adc_log = 0;
    uint32_t now = millis();
    if (now - last_adc_log < 5000) return;
    last_adc_log = now;

#if ADC_BATTERY_VOLTAGE_PIN < 0
    Serial.println("ADC Test: disabled because ADC_BATTERY_VOLTAGE_PIN < 0");
#else
    if constexpr (ADC_BATTERY_ENABLE_PIN >= 0) {
        const bool level = ADC_BATTERY_ENABLE_ACTIVE_LOW ? false : true;
        pinMode(ADC_BATTERY_ENABLE_PIN, OUTPUT);
        digitalWrite(ADC_BATTERY_ENABLE_PIN, level ? HIGH : LOW);
        if (ADC_BATTERY_POWER_SETTLE_MS > 0) delay(ADC_BATTERY_POWER_SETTLE_MS);
    }

    uint32_t total_mv = 0;
    const uint8_t samples = ADC_BATTERY_SAMPLES > 0 ? ADC_BATTERY_SAMPLES : 1;
    for (uint8_t i = 0; i < samples; ++i) {
        total_mv += analogReadMilliVolts(ADC_BATTERY_VOLTAGE_PIN);
        if (i + 1 < samples && ADC_BATTERY_SAMPLE_DELAY_MS > 0) {
            delay(ADC_BATTERY_SAMPLE_DELAY_MS);
        }
    }

    if constexpr (ADC_BATTERY_ENABLE_PIN >= 0) {
        const bool level = ADC_BATTERY_ENABLE_ACTIVE_LOW ? true : false;
        digitalWrite(ADC_BATTERY_ENABLE_PIN, level ? HIGH : LOW);
    }

    uint32_t raw_mv = total_mv / samples;
    int32_t calibrated_mv = (int32_t)((raw_mv * ADC_BATTERY_VOLTAGE_SCALE) + ADC_BATTERY_VOLTAGE_OFFSET_MV + 0.5f);
    if (calibrated_mv < 0) calibrated_mv = 0;

    Serial.printf(
        "ADC Test: pin=%d raw=%lu mV scale=%.3f battery=%ld mV samples=%u\n",
        ADC_BATTERY_VOLTAGE_PIN,
        (unsigned long)raw_mv,
        (double)ADC_BATTERY_VOLTAGE_SCALE,
        (long)calibrated_mv,
        samples
    );
#endif
}
#endif

void updateSources() {
    const uint32_t now = millis();
    bool rs485EnabledForPoll = false;

    if constexpr (HAS_RS485_POWER_CONTROL) {
        bool rs485PollDue = false;
        if constexpr (COLD_USES_RS485) {
            rs485PollDue = rs485PollDue || (coldSrc && coldSrc->isPollDue(now));
        }
        if constexpr (HOT_USES_RS485) {
            rs485PollDue = rs485PollDue || (hotSrc && hotSrc->isPollDue(now));
        }

        if (rs485PollDue) {
            enableRs485ForPoll();
            rs485EnabledForPoll = true;
        }
    }

    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();

    if (rs485EnabledForPoll) {
        const uint32_t afterPollNow = millis();
        const uint32_t nextDelay = millisUntilNextRs485Poll(afterPollNow);

        if (shouldPowerDownRs485(afterPollNow)) {
            if (nextDelay == UINT32_MAX) {
                Serial.println("RS485: no pending polls; powering down.");
            } else {
                Serial.printf(
                    "RS485: next poll in %lu ms exceeds idle-off threshold %lu ms; powering down.\n",
                    (unsigned long)nextDelay,
                    (unsigned long)RS485_POWER_IDLE_OFF_MS
                );
            }
            disableRs485AfterPoll();
        } else {
            Serial.printf(
                "RS485: keeping power ON, next poll in %lu ms (idle-off threshold %lu ms).\n",
                (unsigned long)nextDelay,
                (unsigned long)RS485_POWER_IDLE_OFF_MS
            );
        }
    }
}


void handleZigbeeReporting() {
    static uint32_t boot_time = millis();
    static uint32_t last_heartbeat = 0;
    static bool initial_config_sent = false;
    uint32_t now = millis();

    // Keep Zigbee reports serialized so the stack has time to process each
    // attribute update and outgoing report command.
    if (reportState != IDLE && now >= nextActionTime) {
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
    } else if (hotSrc && hotSrc->hasHourChanged()) {
        reportState = PENDING_HOT_HOURLY;
        nextActionTime = now;
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

// Periodically persists readings even when no Zigbee configuration changed.
void handleAutoSave() {
    static uint32_t last_save = 0;
    if (millis() - last_save >= 900000) { 
        last_save = millis();
        saveConfiguration(); 
    }
}

// Persists user-editable Zigbee attributes immediately after a write.
void handleConfigSave() {
    if (zigbeeCold.isConfigDirty() || zigbeeHot.isConfigDirty()) {
        saveConfiguration();
        zigbeeCold.clearConfigDirty();
        zigbeeHot.clearConfigDirty();
    }
}

void updateStatusIndication() {
    static uint32_t last_blink = 0;
    if (!Zigbee.connected()) {
        if (millis() - last_blink > 500) {
            last_blink = millis();
            static bool t = false; t = !t;
            t ? StatusLedSet(20, 20, 0) : StatusLedSet(0, 0, 0);
        }
    } else {
        StatusLedSet(0, 0, 0);
    }
}

void checkServiceButton() {
    static uint32_t press_start = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (press_start == 0) {
            press_start = millis();
        } else if (millis() - press_start > 3000) {
            StatusLedFlash(50, 0, 0, 1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    } else {
        press_start = 0;
    }
}

#endif  // RS485_UART_BRIDGE
