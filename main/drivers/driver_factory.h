#ifndef DRIVER_FACTORY_H
#define DRIVER_FACTORY_H

#include "smart_driver.h"
#include "pulsar_ds15_20.h"
#include "mock_meter_driver.h"

namespace Driver {
enum class MeterModel {
    Mock,
    Pulsar_Du_15_20,
    Modbus_Generic
};

// Creates protocol drivers for smart-meter sources. Ownership is transferred
// to the caller.
class DriverFactory {
public:
    static SmartMeterDriver* create(MeterModel model, Stream* transport, uint32_t address) {
        switch (model) {
            case MeterModel::Pulsar_Du_15_20:
                return new PulsarDu_15_20(transport, address);
            
            case MeterModel::Mock:
                return new MockMeterDriver();

            default:
                return nullptr;
        }
    }
};
}  // namespace Driver
#endif  // DRIVER_FACTORY_H
