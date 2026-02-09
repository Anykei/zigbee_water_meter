#ifndef SMART_DRIVER_H
#define SMART_DRIVER_H

#include <Arduino.h>
#include <vector>

namespace Driver {
enum class MeterParam {
    TotalVolume,           // Accumulated volume (liters/m3)
    BatteryVoltage,        // Current voltage (Volts)
    
    BatteryThresholdMin,   // Deep discharge threshold (shutdown)
    BatteryThresholdAlarm, // Warning threshold (system alert)
    BatteryThresholdMax,   // Maximum allowable voltage threshold
    
    FlowRateMin,           // Minimum detectable flow rate
    FlowRateMax            // Maximum allowable flow rate
};

// Interface for physical meter drivers (Modbus/RS485).
class SmartMeterDriver {
public:
    virtual ~SmartMeterDriver() {}

    void setTransport(Stream* transport) { _transport = transport; }

    // Sets the port for TX/RX packet logging (any Serial).
    void setLogger(Print* logger) { log_serial = logger; }

    // Sets the device address on the bus.
    virtual void setAddress(uint32_t address) {
         _address = address;
    }

    // Returns a list of parameters that this specific driver can read.
    virtual std::vector<MeterParam> getSupportedParams() const = 0;

    // Main method for retrieving data.
    virtual bool getValue(MeterParam param, float &result) = 0;

protected:
    Stream* _transport = nullptr; // Abstract transport (can be RS485, Modbus, etc.)
    Print* log_serial = nullptr;
    uint32_t _address = 0;
    
    SmartMeterDriver(Stream* transport) : _transport(transport) {}
};

} 

#endif