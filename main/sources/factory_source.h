#ifndef FACTORY_SOURCE_H
#define FACTORY_SOURCE_H

#include "water_source.h"
#include "pulse_source.h"
#include "smart_source.h"
#include "simulation_source.h"
#include "drivers/smart_driver.h"

namespace Source {
    enum class SourceType {
        Pulse,
        Smart,
        Test
    };
    
    // Builds a source for the compile-time channel configuration selected in
    // main.cpp. Ownership is transferred to the caller.
    class SourceFactory {
    public:
        static WaterSource* create(SourceType type, uint64_t initialLiters, uint8_t pin, Driver::SmartMeterDriver* drv) {
            switch (type) {
                case SourceType::Smart:
                    if (drv != nullptr) {
                        SmartSource* src = new SmartSource(drv);

                        if (src == nullptr) {
                            return nullptr;
                        }

                        src->setLiters(initialLiters);
                        return src;
                        
                    }
                    return nullptr;

                case SourceType::Pulse:
                    {
                        PulseSource* src = new PulseSource(pin);
                        src->setLiters(initialLiters);
                        return src;
                    }

                case SourceType::Test:
                    return new SimulationSource(initialLiters);

                default:
                    return nullptr;
            }
        }
    };
}  // namespace Source

#endif  // FACTORY_SOURCE_H
