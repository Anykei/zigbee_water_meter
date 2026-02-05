#ifndef PULSAR_DS15_20_RS485_H
#define PULSAR_DS15_20_RS485_H

#include "smart_driver.h"


namespace Driver {
class PulsarDu_15_20 : public SmartMeterDriver {
public:
    PulsarDu_15_20(Stream* stream, uint32_t address) : SmartMeterDriver(stream) {
        setAddress(address);
    }

    // Сообщаем, что этот Пульсар умеет отдавать
    std::vector<MeterParam> getSupportedParams() const override {
        return {
            MeterParam::TotalVolume,
            MeterParam::BatteryVoltage,
            MeterParam::BatteryThresholdMin,
            MeterParam::BatteryThresholdAlarm
        };
    }

    // Универсальный метод чтения
    bool getValue(MeterParam param, float &result) override {
        if (!_transport) return false; // Защита от разыменования нулевого указателя

        switch (param) {
            case MeterParam::TotalVolume:
                return readTotalValue(result);
            case MeterParam::BatteryVoltage:
                return readParameter(0x000E, result); // Напряжение батареи
            case MeterParam::BatteryThresholdMin:
                return readParameter(0x000F, result); // Порог MIN
            case MeterParam::BatteryThresholdAlarm:
                return readParameter(0x0010, result); // Порог ALARM
            default:
                return false;
        }
    }

    void setAddress(uint32_t address) override {
        _address = address;
        // Упаковка BCD для Пульсара
        _addr[3] = ((address / 1) % 10) | (((address / 10) % 10) << 4);
        _addr[2] = ((address / 100) % 10) | (((address / 1000) % 10) << 4);
        _addr[1] = ((address / 10000) % 10) | (((address / 100000) % 10) << 4);
        _addr[0] = ((address / 1000000) % 10) | (((address / 10000000) % 10) << 4);
    }

private:
    uint8_t _addr[4];

    bool readTotalValue(float &result) {
        uint8_t packet[14];
        int len = 0;
        for(int i=0; i<4; i++) packet[len++] = _addr[i];
        packet[len++] = 0x01; packet[len++] = 0x0E; packet[len++] = 0x01;
        packet[len++] = 0x00; packet[len++] = 0x00; packet[len++] = 0x00;
        packet[len++] = 0x00; packet[len++] = 0x01;
        uint16_t crc = calculateCRC(packet, len);
        packet[len++] = crc & 0xFF; packet[len++] = (crc >> 8) & 0xFF;

        if (log_serial) {
            log_serial->printf(">>> TX [%08u] Vol: ", _address);
            for(int i=0; i<len; i++) log_serial->printf("%02X ", packet[i]);
            log_serial->println();
        }

        while(_transport->available()) _transport->read();
        _transport->write(packet, len); 
        _transport->flush();

        uint8_t res[64];
        size_t rxLen = _transport->readBytes(res, 64);
        
        if (log_serial && rxLen > 0) {
            log_serial->printf("<<< RX [%08u]: ", _address);
            for(int i=0; i<rxLen; i++) log_serial->printf("%02X ", res[i]);
            log_serial->println();
        }

        if (rxLen < 10 || calculateCRC(res, rxLen-2) != (res[rxLen-2]|(res[rxLen-1]<<8))) return false;

        union { uint8_t b[4]; float f; } data;
        memcpy(data.b, &res[6], 4);
        result = data.f;
        return true;
    }

    bool readParameter(uint16_t paramId, float &result) {
        uint8_t packet[12];
        int len = 0;
        for(int i=0; i<4; i++) packet[len++] = _addr[i];
        packet[len++] = 0x0A; packet[len++] = 0x0C;
        packet[len++] = paramId & 0xFF; packet[len++] = (paramId >> 8) & 0xFF;
        
        uint16_t crc = calculateCRC(packet, len);
        packet[len++] = crc & 0xFF; packet[len++] = (crc >> 8) & 0xFF;

        if (log_serial) log_serial->printf(">>> TX [%08u] Param 0x%04X\n", _address, paramId);

        while(_transport->available()) _transport->read();
        _transport->write(packet, len); _transport->flush();

        uint8_t res[18];
        size_t rxLen = _transport->readBytes(res, 18);
        if (rxLen < 18 || calculateCRC(res, 16) != (res[16]|(res[17]<<8))) return false;

        union { uint8_t b[4]; float f; } data;
        memcpy(data.b, &res[6], 4);
        result = data.f;
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

}

#endif