#ifndef DRIVER_FACTORY_H
#define DRIVER_FACTORY_H

#include "smart_driver.h"
#include "pulsar_ds15_20.h"
#include "mock_meter_driver.h"

namespace Driver {
enum class MeterModel {
    Mock,           // Симуляция
    Pulsar_Du_15_20,      // Наш текущий Пульсар
    Modbus_Generic  // (на будущее)
};

class DriverFactory {
public:
    static SmartMeterDriver* create(MeterModel model, Stream* transport, uint32_t address) {
        switch (model) {
            case MeterModel::Pulsar_Du_15_20:
                return new PulsarDu_15_20(transport, address);
            
            case MeterModel::Mock:
                return new MockMeterDriver(); // Ему транспорт не нужен

            default:
                return nullptr;
        }
    }
};
}
#endif