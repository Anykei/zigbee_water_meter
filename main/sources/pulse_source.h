#ifndef PULSE_SOURCE_H
#define PULSE_SOURCE_H

#include "water_source.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Source {

// Counts reed-switch or open-collector pulses. Access to the accumulated
// liters is protected because updates can happen from an ISR.
class PulseSource : public WaterSource {
private:
    uint8_t _pin;
    uint32_t _debounceMs = 50;
    volatile uint64_t _liters = 0; 
    volatile uint32_t _lastPulseTime = 0;
    volatile bool _pulseDetected = false;
    portMUX_TYPE _spinlock = portMUX_INITIALIZER_UNLOCKED;

public:
    PulseSource(uint8_t pin, uint32_t debounceMs = 50, uint64_t initialLiters = 0) 
        : _pin(pin), _debounceMs(debounceMs), _liters(initialLiters) {
        _pollInterval = 60000; 
    }

    void begin() override {
        pinMode(_pin, INPUT_PULLUP);
    }

    // Called from the GPIO ISR, so it must stay short and IRAM-safe.
    void IRAM_ATTR increment() {
        uint32_t now = millis();
        if (now - _lastPulseTime > _debounceMs) {
            portENTER_CRITICAL_ISR(&_spinlock);
            _liters = _liters + 1;
            _lastPulseTime = now;
            portEXIT_CRITICAL_ISR(&_spinlock);
            _pulseDetected = true;
        }
    }

    uint64_t getLiters() override {
        uint64_t temp_liters;
        portENTER_CRITICAL(&_spinlock);
        temp_liters = _liters;
        portEXIT_CRITICAL(&_spinlock);
        return temp_liters;
    }

    void setLiters(uint64_t l) override {
        portENTER_CRITICAL(&_spinlock);
        _liters = l;
        portEXIT_CRITICAL(&_spinlock);
    }

    void update() override {
        if (_pulseDetected) {
            _pulseDetected = false;
        }
        markReadingsValid();
    }
};

}  // namespace Source

#endif  // PULSE_SOURCE_H
