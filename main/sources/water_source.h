#ifndef WATER_SOURCE_H
#define WATER_SOURCE_H

#include <Arduino.h>

namespace Source {
    static constexpr uint16_t kMinPollIntervalMinutes = 1;
    static constexpr uint16_t kMaxPollIntervalMinutes = 1440;
    static constexpr uint32_t kMsPerMinute = 60000;

    // Base class for meter inputs. It owns common accounting windows and lets
    // subclasses focus on hardware-specific reads.
    class WaterSource {
    protected:
        uint32_t _pollInterval = 3000;
        uint32_t _lastPoll = 0;
        
        int32_t  _offset = 0;           
        uint32_t _serialNumber = 0;     
        float    _batteryVoltage = 0;
        bool     _batteryVoltageValid = false;
        float    _flowRateM3PerHour = 0;

        uint64_t _litersAtHourStart = 0;
        uint64_t _litersAtDayStart = 0;
        
        uint64_t _lastCompletedHourLiters = 0;
        uint64_t _lastCompletedDayLiters = 0;

        uint32_t _lastHourCheck = 0;
        uint32_t _lastDayCheck = 0;
        
        bool _hourChanged = false;
        bool _dayChanged = false;
        bool _readingsValid = true;
        bool _freshReportPending = false;
        bool _freshReportReady = false;

        uint32_t _msInHour = 3600000;
        uint32_t _msInDay  = 86400000;

        void invalidateReadings() {
            _readingsValid = false;
            _freshReportReady = false;
        }

        void markReadingsValid() {
            _readingsValid = true;
            if (_freshReportPending) {
                _freshReportPending = false;
                _freshReportReady = true;
            }
        }

        void resetAccountingWindows(uint64_t currentLiters) {
            _litersAtHourStart = currentLiters;
            _litersAtDayStart = currentLiters;
            _lastCompletedHourLiters = 0;
            _lastCompletedDayLiters = 0;
            _lastHourCheck = millis();
            _lastDayCheck = _lastHourCheck;
            _hourChanged = false;
            _dayChanged = false;
        }

    public:
        virtual ~WaterSource() {}

        // Shortens accounting windows for integration tests and demos.
        void setTestMode(bool enabled) {
            _msInHour = enabled ? 10000 : 3600000;
            _msInDay  = enabled ? 20000 : 86400000;
            Serial.printf("Source: Test mode is %s. Hour interval: %lu ms\n", enabled ? "ON" : "OFF", _msInHour);
        }

        void setPollInterval(uint32_t ms) { _pollInterval = ms; }

        void setPollIntervalMinutes(uint32_t minutes) {
            if (minutes < kMinPollIntervalMinutes) minutes = kMinPollIntervalMinutes;
            if (minutes > kMaxPollIntervalMinutes) minutes = kMaxPollIntervalMinutes;

            _pollInterval = minutes * kMsPerMinute;
            forceUpdate();
        }

        uint16_t getPollIntervalMinutes() const {
            uint32_t minutes = (_pollInterval + kMsPerMinute - 1) / kMsPerMinute;
            if (minutes < kMinPollIntervalMinutes) minutes = kMinPollIntervalMinutes;
            if (minutes > kMaxPollIntervalMinutes) minutes = kMaxPollIntervalMinutes;
            return static_cast<uint16_t>(minutes);
        }
        
        void setOffset(int32_t liters) { _offset = liters; }
        int32_t getOffset() const { return _offset; }

        virtual void setSerialNumber(uint32_t sn) { 
            _serialNumber = sn; 
            forceUpdate();
        }
        uint32_t getSerialNumber() const { return _serialNumber; }

        virtual float getBatteryVoltage() const { return _batteryVoltage; }
        bool hasBatteryVoltage() const { return _batteryVoltageValid; }
        uint16_t getBatteryMilliVolts() const {
            if (!_batteryVoltageValid || _batteryVoltage <= 0.0f) return 0;

            const double millivolts = (double)_batteryVoltage * 1000.0;
            if (millivolts >= UINT16_MAX) return UINT16_MAX;
            return (uint16_t)(millivolts + 0.5);
        }
        virtual float getFlowRateM3PerHour() const { return _flowRateM3PerHour; }

        virtual void resetReadingsForNewMeter() {}
        bool hasValidReadings() const { return _readingsValid; }
        bool hasFreshReportReady() const { return _freshReportReady; }
        void clearFreshReportReady() { _freshReportReady = false; }
        void requestFreshReport() {
            _freshReportPending = true;
            forceUpdate();
        }

        // Reported value includes the user-configured calibration offset.
        uint64_t getTotalLiters() {
            const uint64_t raw = getLiters();
            const int64_t offset = _offset;
            if (offset < 0) {
                const uint64_t magnitude = (uint64_t)(-offset);
                return raw > magnitude ? raw - magnitude : 0;
            }

            const uint64_t add = (uint64_t)offset;
            if (raw > UINT64_MAX - add) return UINT64_MAX;
            return raw + add;
        }
        
        uint64_t getLastHourConsumption() const { return _lastCompletedHourLiters; }

        bool hasHourChanged() {
            if (_hourChanged) { _hourChanged = false; return true; }
            return false;
        }

        virtual void begin() = 0;

        bool isPollDue(uint32_t now) const {
            if (_lastHourCheck == 0) return false;
            return now - _lastPoll >= _pollInterval;
        }

        uint32_t millisUntilPoll(uint32_t now) const {
            const uint32_t elapsed = now - _lastPoll;
            if (elapsed >= _pollInterval) return 0;
            return _pollInterval - elapsed;
        }
        
        // Advances accounting windows and polls the underlying source when due.
        void tick() {
            uint32_t now = millis();

            if (_lastHourCheck == 0) {
                _lastHourCheck = now;
                _lastDayCheck = now;
                _litersAtHourStart = getLiters();
                _litersAtDayStart = getLiters();
                return; 
            }

            if (!_readingsValid) {
                if (now - _lastPoll >= _pollInterval) {
                    _lastPoll = now;
                    Serial.println("Source: Polling for new data...");
                    update();
                }
                return;
            }
            
            if (now - _lastHourCheck >= _msInHour) {
                uint64_t current = getLiters();
                _lastCompletedHourLiters = (current >= _litersAtHourStart) ? (current - _litersAtHourStart) : 0;
                
                _litersAtHourStart = current; 
                _lastHourCheck = now;
                _hourChanged = true; 
                
                Serial.printf("Source: Hour closed. Consumed: %llu L\n", _lastCompletedHourLiters);
            }

            if (now - _lastDayCheck >= _msInDay) {
                uint64_t current = getLiters();
                _lastCompletedDayLiters = (current >= _litersAtDayStart) ? (current - _litersAtDayStart) : 0;
                
                _litersAtDayStart = current;
                _lastDayCheck = now;
                _dayChanged = true;
                
                Serial.printf("Source: Day closed. Consumed: %llu L\n", _lastCompletedDayLiters);
            }

            if (now - _lastPoll >= _pollInterval) {
                _lastPoll = now;
                Serial.println("Source: Polling for new data...");
                update();
            }
        }

        virtual void update() = 0; 
        virtual uint64_t getLiters() = 0;
        virtual void setLiters(uint64_t liters) = 0;

        // Restores accounting anchors when persisted snapshots are available.
        void restoreSnapshots(uint64_t hourLiters, uint64_t dayLiters) {
            _litersAtHourStart = hourLiters;
            _litersAtDayStart = dayLiters;
        }

        void forceUpdate() { _lastPoll = millis() - _pollInterval; }
    };
}  // namespace Source
#endif  // WATER_SOURCE_H
