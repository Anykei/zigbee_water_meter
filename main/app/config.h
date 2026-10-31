#pragma once

#include <Arduino.h>
#include "sources/factory_source.h"
#include "drivers/driver_factory.h"

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

namespace App {
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
constexpr uint32_t ZIGBEE_SLEEP_THRESHOLD_MS = 20_ms;
constexpr uint32_t LOOP_IDLE_DELAY = 100_ms;

#else
constexpr uint32_t HEARTBEAT_INTERVAL = 30_mins;
constexpr uint32_t BATTERY_REPORT_INTERVAL = 6_h;
constexpr uint32_t COLD_POOL_INTERVAL = 30_mins;
constexpr uint32_t HOT_POOL_INTERVAL  = 30_mins;
constexpr uint32_t ZIGBEE_SLEEP_THRESHOLD_MS = 20_ms;
constexpr uint32_t LOOP_IDLE_DELAY = 15_s;

constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;
#endif


constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;

// Compile-time flag avoids initializing RS485 hardware in Pulse/Test-only builds.
// Pulse counting relies on edge interrupts; keep the MCU awake in pulse builds.
constexpr bool ALLOW_LIGHT_SLEEP = COLD_TYPE != Source::SourceType::Pulse && HOT_TYPE != Source::SourceType::Pulse;
constexpr bool NEED_RS485 = (COLD_TYPE == Source::SourceType::Smart || HOT_TYPE == Source::SourceType::Smart);
constexpr bool COLD_USES_RS485 = COLD_TYPE == Source::SourceType::Smart;
constexpr bool HOT_USES_RS485 = HOT_TYPE == Source::SourceType::Smart;
constexpr bool HAS_RS485_POWER_CONTROL = NEED_RS485 && RS485_POWER_CONTROL_ENABLED && (RS485_POWER_PIN >= 0);

}  // namespace App
