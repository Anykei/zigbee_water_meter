#ifndef PULSAR_RS485_H
#define PULSAR_RS485_H

#include <Arduino.h>

class PulsarRS485 {
private:
    HardwareSerial* _serial;
    uint8_t _addr[4];
    int _re_de_pin;
    uint32_t _address;

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

public:
    Stream* log_serial;
    PulsarRS485(HardwareSerial* serial, uint32_t address, int re_de_pin = -1) {
        _serial = serial; _re_de_pin = re_de_pin;
        _address = address;
        setSerialNumber(address);
    }

    void setSerialNumber(uint32_t address) {
      _address = address;
      _addr[3] = ((address / 1) % 10) | (((address / 10) % 10) << 4);         // Цифры 3 и 9 -> 0x39
      _addr[2] = ((address / 100) % 10) | (((address / 1000) % 10) << 4);     // Цифры 8 и 9 -> 0x89
      _addr[1] = ((address / 10000) % 10) | (((address / 100000) % 10) << 4); // Цифры 1 и 2 -> 0x12
      _addr[0] = ((address / 1000000) % 10) | (((address / 10000000) % 10) << 4); // Цифры 1 и 0 -> 0x10
    }

    // Чтение показаний (Функция 0x01)
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
            log_serial->printf(">>> TX [%08X]: ", _address);
            for(int i=0; i<len; i++) log_serial->printf("%02X ", packet[i]);
            log_serial->println();
        }

        _serial->write(packet, len); _serial->flush();

        uint8_t res[64];
        size_t rxLen = _serial->readBytes(res, 64);
        if (rxLen < 10 || calculateCRC(res, rxLen-2) != (res[rxLen-2]|(res[rxLen-1]<<8))) return false;

        union { uint8_t b[4]; float f; } data;
        memcpy(data.b, &res[6], 4);
        result = data.f;
        return true;
    }

    // Чтение системного параметра (Функция 0x0A)
    bool readParameter(uint16_t paramId, float &result) {
        uint8_t packet[12];
        int len = 0;
        for(int i=0; i<4; i++) packet[len++] = _addr[i];
        packet[len++] = 0x0A; // Func: Read Parameter
        packet[len++] = 0x0C; // Packet length
        packet[len++] = paramId & 0xFF; // Param ID Low
        packet[len++] = (paramId >> 8) & 0xFF; // Param ID High
        
        uint16_t crc = calculateCRC(packet, len);
        packet[len++] = crc & 0xFF; packet[len++] = (crc >> 8) & 0xFF;

        while(_serial->available()) _serial->read();
        // if (_re_de_pin != -1) digitalWrite(_re_de_pin, HIGH);
        _serial->write(packet, len); _serial->flush();
        // if (_re_de_pin != -1) digitalWrite(_re_de_pin, LOW);

        uint8_t res[18];

        

        size_t rxLen = _serial->readBytes(res, 18);
        if (rxLen < 18) return false;
        if (calculateCRC(res, 16) != (res[16]|(res[17]<<8))) return false;

        // В ответе 0x0A данные лежат в байтах 6, 7, 8, 9 (если это Float32)
        union { uint8_t b[4]; float f; } data;
        memcpy(data.b, &res[6], 4);
        result = data.f;
        return true;
    }
};

#endif