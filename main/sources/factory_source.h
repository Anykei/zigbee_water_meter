#ifndef FACTORY_SOURCE_H
#define FACTORY_SOURCE_H

#include "water_source.h"
#include "pulse_source.h"
#include "smart_source.h"
#include "simulation_source.h" // Не забудь создать этот файл для тестов
#include "drivers/smart_driver.h"

namespace Source {
    enum class SourceType {
        Pulse,
        Smart,
        Test   // Новый тип
    };
    
    class SourceFactory {
    public:
        static WaterSource* create(SourceType type, uint64_t initialLiters, uint8_t pin, Driver::SmartMeterDriver* drv) {
            switch (type) {
                case SourceType::Smart:
                    if (drv != nullptr) {
                        // Драйвер уже пришел с настроенным серийником из своей фабрики
                        SmartSource* src = new SmartSource(drv);
                        src->setLiters(initialLiters); // Восстанавливаем показания
                        return src;
                    }
                    return nullptr;

                case SourceType::Pulse:
                    {
                        PulseSource* src = new PulseSource(pin);
                        src->setLiters(initialLiters); // Восстанавливаем показания
                        return src;
                    }

                case SourceType::Test:
                    return new SimulationSource(initialLiters);

                default:
                    return nullptr;
            }
        }
    };
}

#endif