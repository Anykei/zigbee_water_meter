#ifndef PULSAR_DS15_20_RS485_H
#define PULSAR_DS15_20_RS485_H

#include "smart_driver.h"

#ifndef PULSAR_FLOW_RATE_K_INDEX
#define PULSAR_FLOW_RATE_K_INDEX 3
#endif

#if PULSAR_FLOW_RATE_K_INDEX < 1 || PULSAR_FLOW_RATE_K_INDEX > 5
#error "PULSAR_FLOW_RATE_K_INDEX must be in the 1..5 range"
#endif

namespace Driver {
// Driver for Pulsar Du 15/20 meters using the vendor RS485 packet format.
class PulsarDu_15_20 : public SmartMeterDriver {
public:
    PulsarDu_15_20(Stream* stream, uint32_t address) : SmartMeterDriver(stream) {
        setAddress(address);
    }

    std::vector<MeterParam> getSupportedParams() const override {
        return {
            MeterParam::TotalVolume,
            MeterParam::BatteryVoltage,
            MeterParam::FlowRate,
            MeterParam::BatteryThresholdMin,
            MeterParam::BatteryThresholdAlarm,
            MeterParam::BatteryThresholdMax
        };
    }

    bool getValue(MeterParam param, float &result) override {
        if (!_transport) return false;

        switch (param) {
            case MeterParam::TotalVolume:
                return readTotalValue(result);
            case MeterParam::BatteryVoltage:
                return readParameterMillivolts(0x0040, result);
            case MeterParam::FlowRate:
                return readFlowRate(result);
            case MeterParam::BatteryThresholdMin:
                return readParameterMillivolts(0x0041, result);
            case MeterParam::BatteryThresholdAlarm:
                return readParameterMillivolts(0x0042, result);
            case MeterParam::BatteryThresholdMax:
                return readParameterMillivolts(0x0043, result);
            default:
                return false;
        }
    }

    void setAddress(uint32_t address) override {
        _address = address;
        _currentValuesValid = false;
        // Pulsar addresses are encoded as four packed-BCD bytes.
        _addr[3] = ((address / 1) % 10) | (((address / 10) % 10) << 4);
        _addr[2] = ((address / 100) % 10) | (((address / 1000) % 10) << 4);
        _addr[1] = ((address / 10000) % 10) | (((address / 100000) % 10) << 4);
        _addr[0] = ((address / 1000000) % 10) | (((address / 10000000) % 10) << 4);
    }

private:
    uint8_t _addr[4];
    uint16_t _requestId = 0;
    float _currentValues[5] = {};
    uint32_t _currentValuesReadAt = 0;
    bool _currentValuesValid = false;

    bool readTotalValue(float &result) {
        if (!readCurrentValues()) return false;

        result = _currentValues[0];
        return true;
    }

    bool readFlowRate(float &result) {
        if (!readCurrentValues()) return false;

        result = _currentValues[PULSAR_FLOW_RATE_K_INDEX - 1];
        return true;
    }

    bool readCurrentValues() {
        uint32_t now = millis();
        if (_currentValuesValid && (now - _currentValuesReadAt) < 1000) {
            return true;
        }

        uint8_t packet[14];
        int len = 0;
        for(int i=0; i<4; i++) packet[len++] = _addr[i];
        packet[len++] = 0x01; packet[len++] = 0x0E;
        packet[len++] = 0x1F; packet[len++] = 0x00; packet[len++] = 0x00; packet[len++] = 0x00;
        const uint16_t requestId = _requestId++;
        packet[len++] = requestId & 0xFF; packet[len++] = (requestId >> 8) & 0xFF;
        uint16_t crc = calculateCRC(packet, len);
        packet[len++] = crc & 0xFF; packet[len++] = (crc >> 8) & 0xFF;

        if (log_serial) {
            log_serial->printf(">>> TX [%08lu] Current: ", (unsigned long)_address);
            for(int i=0; i<len; i++) log_serial->printf("%02X ", packet[i]);
            log_serial->println();
        }

        while(_transport->available()) _transport->read();
        _transport->write(packet, len); 
        _transport->flush();

        uint8_t res[64];
        size_t rxLen = _transport->readBytes(res, 64);
        
        if (log_serial && rxLen > 0) {
            log_serial->printf("<<< RX [%08lu]: ", (unsigned long)_address);
            for(int i=0; i<rxLen; i++) log_serial->printf("%02X ", res[i]);
            log_serial->println();
        }

        if (rxLen < 30 || calculateCRC(res, rxLen - 2) != (res[rxLen - 2] | (res[rxLen - 1] << 8))) {
            if (log_serial) {
                log_serial->printf(
                    "!!! RX [%08lu] Current invalid: len=%u\n",
                    (unsigned long)_address,
                    (unsigned)rxLen
                );
            }
            _currentValuesValid = false;
            return false;
        }

        const size_t requestIdOffset = rxLen - 4;
        if (
            memcmp(res, _addr, sizeof(_addr)) != 0 ||
            res[4] != 0x01 ||
            res[5] != rxLen ||
            res[requestIdOffset] != (requestId & 0xFF) ||
            res[requestIdOffset + 1] != ((requestId >> 8) & 0xFF)
        ) {
            if (log_serial) {
                log_serial->printf(
                    "!!! RX [%08lu] Current rejected: addr_ok=%d cmd=0x%02X len_field=%u len=%u request_id_ok=%d\n",
                    (unsigned long)_address,
                    memcmp(res, _addr, sizeof(_addr)) == 0,
                    res[4],
                    res[5],
                    (unsigned)rxLen,
                    res[requestIdOffset] == (requestId & 0xFF) &&
                        res[requestIdOffset + 1] == ((requestId >> 8) & 0xFF)
                );
            }
            _currentValuesValid = false;
            return false;
        }

        for (size_t i = 0; i < 5; ++i) {
            memcpy(&_currentValues[i], &res[6 + (i * sizeof(float))], sizeof(float));
        }
        _currentValuesReadAt = now;
        _currentValuesValid = true;

        if (log_serial) {
            log_serial->printf(
                "<<< Parsed [%08lu] K1=%.6f K2=%.6f K3=%.6f K4=%.6f K5=%.6f\n",
                (unsigned long)_address,
                (double)_currentValues[0],
                (double)_currentValues[1],
                (double)_currentValues[2],
                (double)_currentValues[3],
                (double)_currentValues[4]
            );
        }
        return true;
    }

    bool readParameterFrame(uint16_t paramId, uint8_t *res, size_t resSize, size_t &rxLen) {
        uint8_t packet[12];
        int len = 0;
        for(int i=0; i<4; i++) packet[len++] = _addr[i];
        packet[len++] = 0x0A; packet[len++] = 0x0C;
        packet[len++] = paramId & 0xFF; packet[len++] = (paramId >> 8) & 0xFF;
        const uint16_t requestId = _requestId++;
        packet[len++] = requestId & 0xFF; packet[len++] = (requestId >> 8) & 0xFF;
        
        uint16_t crc = calculateCRC(packet, len);
        packet[len++] = crc & 0xFF; packet[len++] = (crc >> 8) & 0xFF;

        if (log_serial) {
            log_serial->printf(">>> TX [%08lu] Param 0x%04X: ", (unsigned long)_address, paramId);
            for(int i=0; i<len; i++) log_serial->printf("%02X ", packet[i]);
            log_serial->println();
        }

        while(_transport->available()) _transport->read();
        _transport->write(packet, len); _transport->flush();

        rxLen = _transport->readBytes(res, resSize);
        if (log_serial && rxLen > 0) {
            log_serial->printf("<<< RX [%08lu] Param 0x%04X: ", (unsigned long)_address, paramId);
            for(int i=0; i<rxLen; i++) log_serial->printf("%02X ", res[i]);
            log_serial->println();
        }

        if (rxLen < 10 || calculateCRC(res, rxLen - 2) != (res[rxLen - 2] | (res[rxLen - 1] << 8))) {
            if (log_serial) {
                log_serial->printf(
                    "!!! RX [%08lu] Param 0x%04X invalid: len=%u\n",
                    (unsigned long)_address,
                    paramId,
                    (unsigned)rxLen
                );
            }
            return false;
        }
        if (
            memcmp(res, _addr, sizeof(_addr)) != 0 ||
            res[4] != 0x0A ||
            res[5] != rxLen ||
            rxLen < 16 ||
            res[14] != (requestId & 0xFF) ||
            res[15] != ((requestId >> 8) & 0xFF)
        ) {
            if (log_serial) {
                log_serial->printf(
                    "!!! RX [%08lu] Param 0x%04X rejected: addr_ok=%d cmd=0x%02X len_field=%u len=%u request_id_ok=%d",
                    (unsigned long)_address,
                    paramId,
                    memcmp(res, _addr, sizeof(_addr)) == 0,
                    res[4],
                    res[5],
                    (unsigned)rxLen,
                    rxLen >= 16 && res[14] == (requestId & 0xFF) && res[15] == ((requestId >> 8) & 0xFF)
                );
                if (rxLen >= 7 && res[4] == 0x00) {
                    log_serial->printf(" error=0x%02X", res[6]);
                }
                log_serial->println();
            }
            return false;
        }
        return true;
    }

    bool readParameterMillivolts(uint16_t paramId, float &result) {
        uint16_t millivolts = 0;
        if (!readParameterUInt16(paramId, millivolts)) return false;

        result = millivolts / 1000.0f;
        return true;
    }

    bool readParameterFloat(uint16_t paramId, float &result) {
        uint8_t res[64];
        size_t rxLen = 0;
        if (!readParameterFrame(paramId, res, sizeof(res), rxLen)) return false;

        union { uint8_t b[4]; float f; } data;
        memcpy(data.b, &res[6], 4);
        result = data.f;
        if (log_serial) {
            log_serial->printf(
                "<<< Parsed [%08lu] Param 0x%04X = %.3f\n",
                (unsigned long)_address,
                paramId,
                (double)result
            );
        }
        return true;
    }

    bool readParameterUInt16(uint16_t paramId, uint16_t &result) {
        uint8_t res[64];
        size_t rxLen = 0;
        if (!readParameterFrame(paramId, res, sizeof(res), rxLen)) return false;

        result = res[6] | (res[7] << 8);
        if (log_serial) {
            log_serial->printf(
                "<<< Parsed [%08lu] Param 0x%04X = %u\n",
                (unsigned long)_address,
                paramId,
                (unsigned)result
            );
        }
        return true;
    }

    uint16_t calculateCRC(uint8_t *data, uint16_t len) {
        uint16_t crc = 0xFFFF;
        for (uint16_t pos = 0; pos < len; pos++) {
            crc ^= (uint16_t)data[pos];
            for (int i = 8; i != 0; i--) {
                if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
                else { crc >>= 1; }
            }
        }
        return crc;
    }
};

}  // namespace Driver

#endif  // PULSAR_DS15_20_RS485_H
