#ifndef PULSE_SOURCE_H
#define PULSE_SOURCE_H

#include "water_source.h"

namespace Source {

class PulseSource : public WaterSource {
private:
    uint8_t _pin;
    volatile uint64_t _liters = 0;
    
    // Параметры антидребезга
    uint32_t _debounceMs = 50; 
    volatile uint32_t _lastPulseTime = 0;
    volatile bool _pulseDetected = false;

public:
    /**
     * @param pin Пин геркона
     * @param debounceMs Время антидребезга (50-100мс оптимально для геркона)
     * @param initialLiters Начальное значение из памяти
     */
    PulseSource(uint8_t pin, uint32_t debounceMs = 50, uint64_t initialLiters = 0) 
        : _pin(pin), _debounceMs(debounceMs), _liters(initialLiters) {
        // У импульсного датчика интервал "обновления" может быть больше, 
        // так как данные инкрементальные
        _pollInterval = 60000; 
    }

    void begin() override {
        pinMode(_pin, INPUT_PULLUP);
        // Регистрация прерывания вынесена в main.ino для гибкости
    }

    // Метод, который вызывается ИЗ ПРЕРЫВАНИЯ (ISR)
    // Помечен IRAM_ATTR для работы из оперативной памяти ESP32
    void IRAM_ATTR increment() {
        uint32_t now = millis();
        // Проверка антидребезга
        if (now - _lastPulseTime > _debounceMs) {
            _liters++;
            _lastPulseTime = now;
            _pulseDetected = true;
        }
    }

    uint64_t getLiters() override {
        return _liters;
    }

    void setLiters(uint64_t l) override {
        _liters = l;
    }

    // Вызывается базовым классом WaterSource::tick() раз в _pollInterval
    void update() override {
        if (_pulseDetected) {
            _pulseDetected = false;
            // Здесь можно добавить логику записи в лог или 
            // выставления флага для сохранения в NVS (Preferences)
        }
    }
};

} // namespace Source

#endif