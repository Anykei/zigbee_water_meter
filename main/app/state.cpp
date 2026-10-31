// Copyright 2026 Andrey Nemenko.
#ifndef RS485_UART_BRIDGE
#include "config.h"
#include "state.h"

namespace App {

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


std::unique_ptr<Source::WaterSource> coldSrc;
std::unique_ptr<Source::WaterSource> hotSrc;

}  // namespace App
#endif  // RS485_UART_BRIDGE
