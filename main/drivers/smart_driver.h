#ifndef SMART_DRIVER_H
#define SMART_DRIVER_H

#include <Arduino.h>
#include <vector>

namespace Driver {
enum class MeterParam {
    TotalVolume,           // Накопленный объем (литры/кубы)
    BatteryVoltage,        // Текущее напряжение (Вольты)
    
    BatteryThresholdMin,   // Порог глубокого разряда (отключение)
    BatteryThresholdAlarm, // Порог предупреждения (сообщение в систему)
    BatteryThresholdMax,   // Порог максимально допустимого напряжения
    
    FlowRateMin,           // Минимальный детектируемый расход
    FlowRateMax            // Максимальный допустимый расход
};

class SmartMeterDriver {
public:
    virtual ~SmartMeterDriver() {}

    void setTransport(Stream* transport) { _transport = transport; }

    // Установка порта для вывода TX/RX пакетов (любой Serial)
    void setLogger(Print* logger) { log_serial = logger; }

    // Установка адреса устройства на шине
    virtual void setAddress(uint32_t address) {
         _address = address;
    }

    // Возвращает список параметров, которые этот конкретный драйвер умеет читать
    virtual std::vector<MeterParam> getSupportedParams() const = 0;

    // Главный метод получения данных
    virtual bool getValue(MeterParam param, float &result) = 0;

protected:
    Stream* _transport = nullptr; // Абстрактный транспорт (может быть RS485, Modbus, и т.д.)
    Print* log_serial = nullptr;
    uint32_t _address = 0;
    
    SmartMeterDriver(Stream* transport) : _transport(transport) {}
};

} 

#endif