#ifndef WATER_SOURCE_H
#define WATER_SOURCE_H

#include <Arduino.h>

class WaterSource {
protected:
    uint32_t _pollInterval = 30000; // По умолчанию 30 сек
    uint32_t _lastPoll = 0;
    
public:
    virtual ~WaterSource() {}

    // Геттеры и сеттеры для интервала теперь доступны для всех
    void setPollInterval(uint32_t ms) { _pollInterval = ms; }
    uint32_t getPollInterval() const { return _pollInterval; }

    virtual void begin() = 0;
    
    // Основной метод, который вызывается в loop
    virtual void tick() {
        uint32_t now = millis();
        if (now - _lastPoll >= _pollInterval) {
            _lastPoll = now;
            update(); // Вызываем специфичную для каждого типа логику
        }
    }

    // Чисто виртуальные методы для реализации в потомках
    virtual void update() = 0; 
    virtual uint64_t getLiters() = 0;
    virtual void setLiters(uint64_t liters) = 0;
    virtual void setSerialNumber(uint32_t sn) {}
    
    // Добавим метод для принудительного опроса
    void forceUpdate() { _lastPoll = millis() - _pollInterval; }
};

#endif