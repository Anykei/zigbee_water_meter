#pragma once

#include <memory>
#include "sources/water_source.h"
#include "zigbee_water_meter.h"
#include "zigbee_device_power.h"

namespace App {
// Sources outlive their endpoints' use of them. Protocol drivers are owned by metering.cpp.
extern std::unique_ptr<Source::WaterSource> coldSrc;
extern std::unique_ptr<Source::WaterSource> hotSrc;
extern ZigbeeWaterMeter zigbeeCold;
extern ZigbeeWaterMeter zigbeeHot;
extern ZigbeeDevicePower zigbeePower;
}  // namespace App
