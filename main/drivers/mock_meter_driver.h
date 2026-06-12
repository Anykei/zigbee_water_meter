#ifndef MOCK_METER_DRIVER_H
#define MOCK_METER_DRIVER_H

#include "smart_driver.h"
#include <cmath>

namespace Driver {
    // Lightweight driver for exercising SmartSource without a transport.
    class MockMeterDriver : public SmartMeterDriver {
    private:
        float _mockVol = 100.0;

    public:
        MockMeterDriver() : SmartMeterDriver(nullptr) {}

        std::vector<MeterParam> getSupportedParams() const override {
            return { MeterParam::TotalVolume, MeterParam::BatteryVoltage };
        }

        void setAddress(uint32_t address) override { _address = address; }

        bool getValue(MeterParam param, float &result) override {
            if (param == MeterParam::TotalVolume) {
                _mockVol += rand() % 10 / 1000.0f;
                result = _mockVol;
                return true;
            }
            if (param == MeterParam::BatteryVoltage) {
                result = 3.6f + 0.1f * sin(millis() / 5000.0f); 
                return true;
            }
            return false;
        }
    };
}  // namespace Driver

#endif  // MOCK_METER_DRIVER_H
