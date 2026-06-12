#ifndef SMART_SOURCE_H
#define SMART_SOURCE_H

#include <Arduino.h>
#include "water_source.h"
#include "drivers/smart_driver.h"

namespace Source {
    // Water source backed by a smart meter driver such as an RS485 meter.
    class SmartSource : public WaterSource {
    private:
        Driver::SmartMeterDriver* _drv;
        uint64_t _liters = 0;
        bool _reanchorAfterNextRead = false;

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

        void resetReadingsForNewMeter() override {
            _liters = 0;
            _batteryVoltage = 0;
            _batteryVoltageValid = false;
            _flowRateM3PerHour = 0;
            _reanchorAfterNextRead = true;
            invalidateReadings();
            requestFreshReport();
        }

        void update() override {
            if (!_drv) return;
            if (_serialNumber == 0) {
                Serial.println("Source: Smart meter serial number is not configured; skipping poll.");
                return;
            }

            float volumeM3 = 0;
            if (_drv->getValue(Driver::MeterParam::TotalVolume, volumeM3)) {
                _liters = (uint64_t)(volumeM3 * 1000.0f);
                if (_reanchorAfterNextRead) {
                    resetAccountingWindows(_liters);
                    _reanchorAfterNextRead = false;
                }
                markReadingsValid();
            }

            float flowRateM3PerHour = 0;
            if (_drv->getValue(Driver::MeterParam::FlowRate, flowRateM3PerHour)) {
                _flowRateM3PerHour = flowRateM3PerHour;
                Serial.printf(
                    "Source: Smart meter [%lu] flow rate: %.3f m3/h\n",
                    (unsigned long)_serialNumber,
                    (double)flowRateM3PerHour
                );
            }

            float batteryVoltage = 0;
            if (_drv->getValue(Driver::MeterParam::BatteryVoltage, batteryVoltage)) {
                _batteryVoltage = batteryVoltage;
                _batteryVoltageValid = true;
                Serial.printf(
                    "Source: Smart meter [%lu] battery voltage: %.3f V\n",
                    (unsigned long)_serialNumber,
                    (double)_batteryVoltage
                );
            } else {
                _batteryVoltageValid = false;
                Serial.printf(
                    "Source: Smart meter [%lu] battery voltage read failed.\n",
                    (unsigned long)_serialNumber
                );
            }
        }
    };
}  // namespace Source
#endif  // SMART_SOURCE_H
