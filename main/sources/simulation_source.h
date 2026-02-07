#ifndef SIMULATION_SOURCE_H
#define SIMULATION_SOURCE_H

#include "water_source.h"

namespace Source {

class SimulationSource : public WaterSource {
private:
    uint64_t _liters = 0;
    uint32_t _lastUpdate = 0;
    float _flowRate = 0.1; // Литров в секунду (имитация расхода)

public:
    SimulationSource(uint64_t startValue = 0) : _liters(startValue) {}

    void begin() override {
        _lastUpdate = millis();
    }

    uint64_t getLiters() override { return _liters; }
    void setLiters(uint64_t l) override { _liters = l; }

    void update() override {
        uint32_t now = millis();
        // Каждую секунду добавляем "расход"
        if (now - _lastUpdate > 1000) {
            _liters += rand() % 10 + 1; // Добавляем 1 литр в секунду для наглядности
            _lastUpdate = now;
        }
    }
};

}
#endif