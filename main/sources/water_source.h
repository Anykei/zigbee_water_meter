#ifndef WATER_SOURCE_H
#define WATER_SOURCE_H

#include <Arduino.h>

namespace Source {
    // Abstract base class for water consumption data sources.
    //
    // Provides common logic for tracking hourly/daily consumption, offsets,
    // and serial numbers. Concrete implementations handle the specific hardware.
    class WaterSource {
    protected:
        uint32_t _pollInterval = 3000;
        uint32_t _lastPoll = 0;
        
        int32_t  _offset = 0;           
        uint32_t _serialNumber = 0;     
        float    _batteryVoltage = 0;

        // Reference points (in liters)
        uint64_t _litersAtHourStart = 0;
        uint64_t _litersAtDayStart = 0;
        
        // Results of closed periods
        uint64_t _lastCompletedHourLiters = 0;
        uint64_t _lastCompletedDayLiters = 0;

        // Timers
        uint32_t _lastHourCheck = 0;
        uint32_t _lastDayCheck = 0;
        
        // Event flags for Zigbee
        bool _hourChanged = false;
        bool _dayChanged = false;

        uint32_t _msInHour = 3600000; // 1 hour
        uint32_t _msInDay  = 86400000; // 24 hours

    public:
        virtual ~WaterSource() {}

        // Sets short intervals for testing purposes.
        void setTestMode(bool enabled) {
            _msInHour = enabled ? 10000 : 3600000; // 10 seconds if test mode is on
            _msInDay  = enabled ? 20000 : 86400000; // 20 seconds if test mode is on
            Serial.printf("Source: Test mode is %s. Hour interval: %lu ms\n", enabled ? "ON" : "OFF", _msInHour);
        }

        void setPollInterval(uint32_t ms) { _pollInterval = ms; }
        
        void setOffset(int32_t liters) { _offset = liters; }
        int32_t getOffset() const { return _offset; }

        virtual void setSerialNumber(uint32_t sn) { 
            _serialNumber = sn; 
            _lastPoll = 0; // Force immediate update with new SN
        }
        uint32_t getSerialNumber() const { return _serialNumber; }

        virtual float getBatteryVoltage() const { return _batteryVoltage; }

        // Total value for reporting (Raw + Offset)
        uint64_t getTotalLiters() { return getLiters() + (int64_t)_offset; }
        
        // Get total for the LAST COMPLETED hour
        uint64_t getLastHourConsumption() const { return _lastCompletedHourLiters; }

        // Check flag (with auto-reset)
        bool hasHourChanged() {
            if (_hourChanged) { _hourChanged = false; return true; }
            return false;
        }

        virtual void begin() = 0;
        
        // Main logic loop. Should be called frequently.
        void tick() {
            uint32_t now = millis();

            // Initialize timers on first run
            if (_lastHourCheck == 0) {
                _lastHourCheck = now;
                _lastDayCheck = now;
                _litersAtHourStart = getLiters();
                _litersAtDayStart = getLiters();
                return; 
            }
            
            // 1. Hour closing logic
            if (now - _lastHourCheck >= _msInHour) {
                uint64_t current = getLiters();
                _lastCompletedHourLiters = (current >= _litersAtHourStart) ? (current - _litersAtHourStart) : 0;
                
                _litersAtHourStart = current; 
                _lastHourCheck = now;
                _hourChanged = true; 
                
                Serial.printf("Source: Hour closed. Consumed: %llu L\n", _lastCompletedHourLiters);
            }

            // 2. Day closing logic
            if (now - _lastDayCheck >= _msInDay) {
                uint64_t current = getLiters();
                _lastCompletedDayLiters = (current >= _litersAtDayStart) ? (current - _litersAtDayStart) : 0;
                
                _litersAtDayStart = current;
                _lastDayCheck = now;
                _dayChanged = true;
                
                Serial.printf("Source: Day closed. Consumed: %llu L\n", _lastCompletedDayLiters);
            }

            // 3. Standard hardware polling (driver)
            if (now - _lastPoll >= _pollInterval) {
                _lastPoll = now;
                Serial.println("Source: Polling for new data...");
                update();
            }
        }

        virtual void update() = 0; 
        virtual uint64_t getLiters() = 0;
        virtual void setLiters(uint64_t liters) = 0;

        // Restore snapshots from Preferences at startup
        void restoreSnapshots(uint64_t hourLiters, uint64_t dayLiters) {
            _litersAtHourStart = hourLiters;
            _litersAtDayStart = dayLiters;
        }

        void forceUpdate() { _lastPoll = millis() - _pollInterval; }
    };
}
#endif