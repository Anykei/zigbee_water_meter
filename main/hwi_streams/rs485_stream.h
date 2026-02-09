#ifndef RS485_STREAM_H
#define RS485_STREAM_H

#include <Arduino.h>

// Wrapper around HardwareSerial to handle RS485 Direction Enable (DE) pin.
// Automatically toggles the DE pin when writing data.
class RS485Stream : public Stream {
private:
    HardwareSerial* _serial;
    int _de_pin;

public:
    RS485Stream(HardwareSerial* serial, int de_pin) : _serial(serial), _de_pin(de_pin) {}

    void begin(unsigned long baud, uint32_t config, int rx, int tx) {
        _serial->begin(baud, config, rx, tx);
        if (_de_pin != -1) {
            pinMode(_de_pin, OUTPUT);
            digitalWrite(_de_pin, LOW); 
        }
    }

    size_t write(uint8_t b) override {
        return write(&b, 1);
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (_de_pin != -1) digitalWrite(_de_pin, HIGH); 
        
        size_t n = _serial->write(buffer, size);
        _serial->flush(); 
        
        if (_de_pin != -1) digitalWrite(_de_pin, LOW);
        return n;
    }

    int available() override { return _serial->available(); }
    int read() override { return _serial->read(); }
    int peek() override { return _serial->peek(); }
    void flush() override { _serial->flush(); }

    void setTimeout(unsigned long timeout) { _serial->setTimeout(timeout); }
};

#endif