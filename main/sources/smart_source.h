#ifndef SMART_SOURCE_H
#define SMART_SOURCE_H

#include <Arduino.h>
#include "water_source.h"
#include "drivers/smart_driver.h"

namespace Source {
    class SmartSource : public WaterSource {
    private:
        Driver::SmartMeterDriver* _drv;
        uint64_t _liters = 0;

    public:
        SmartSource(Driver::SmartMeterDriver* drv, uint64_t initialLiters = 0) 
            : _drv(drv), _liters(initialLiters) {}

        void begin() override {
            _lastPoll = millis() - _pollInterval; 
        }

        void setSerialNumber(uint32_t sn) override {
            WaterSource::setSerialNumber(sn); 
            if (_drv) _drv->setAddress(sn);
        }

        uint64_t getLiters() override { return _liters; }
        void setLiters(uint64_t l) override { _liters = l; }

        void update() override {
            if (!_drv) return;

            // 1. Читаем литры
            float volumeM3 = 0;
            if (_drv->getValue(Driver::MeterParam::TotalVolume, volumeM3)) {
                _liters = (uint64_t)(volumeM3 * 1000.0f);
            }

            // 2. Читаем батарейку (раз в цикл опроса)
            // float vBat = 0;
            // if (_drv->getValue(Driver::MeterParam::BatteryVoltage, vBat)) {
            //     _batteryVoltage = vBat; 
            // }
        }
    };
}
#endif