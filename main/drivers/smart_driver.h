#ifndef SMART_DRIVER_H
#define SMART_DRIVER_H

#include <Arduino.h>
#include <vector>

namespace Driver {
enum class MeterParam {
    TotalVolume,           // Accumulated volume (liters/m3)
    BatteryVoltage,        // Current voltage (Volts)
    FlowRate,              // Current flow rate (m3/h)
    
    BatteryThresholdMin,   // Deep discharge threshold (shutdown)
    BatteryThresholdAlarm, // Warning threshold (system alert)
    BatteryThresholdMax,   // Maximum allowable voltage threshold
    
    FlowRateMin,           // Minimum detectable flow rate
    FlowRateMax            // Maximum allowable flow rate
};

// Interface for meter protocols that can expose numeric readings over a
// transport such as RS485.
class SmartMeterDriver {
public:
    virtual ~SmartMeterDriver() {}

    void setTransport(Stream* transport) { _transport = transport; }

    void setLogger(Print* logger) { log_serial = logger; }

    virtual void setAddress(uint32_t address) {
         _address = address;
    }

    virtual std::vector<MeterParam> getSupportedParams() const = 0;

    virtual bool getValue(MeterParam param, float &result) = 0;

protected:
    Stream* _transport = nullptr;
    Print* log_serial = nullptr;
    uint32_t _address = 0;
    
    SmartMeterDriver(Stream* transport) : _transport(transport) {}
};

}  // namespace Driver

#endif  // SMART_DRIVER_H
