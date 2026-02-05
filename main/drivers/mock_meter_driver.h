#ifndef MOCK_METER_DRIVER_H
#define MOCK_METER_DRIVER_H

#include "smart_driver.h"
#include <cmath>

namespace Driver {
    class MockMeterDriver : public SmartMeterDriver {
    private:
        float _mockVol = 100.0;

    public:
        MockMeterDriver() : SmartMeterDriver(nullptr) {} // Транспорт не нужен

        std::vector<MeterParam> getSupportedParams() const override {
            return { MeterParam::TotalVolume, MeterParam::BatteryVoltage };
        }

        void setAddress(uint32_t address) override { _address = address; }

        bool getValue(MeterParam param, float &result) override {
            if (param == MeterParam::TotalVolume) {
                _mockVol += 0.001; // Имитируем медленный расход
                result = _mockVol;
                return true;
            }
            if (param == MeterParam::BatteryVoltage) {
                // Имитируем красивую синусоиду напряжения батарейки
                result = 3.6f + 0.1f * sin(millis() / 5000.0f); 
                return true;
            }
            return false;
        }
    };
}

#endif