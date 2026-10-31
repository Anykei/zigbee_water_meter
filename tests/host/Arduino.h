#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <algorithm>
inline uint32_t testNow = 100;
inline uint32_t millis() { return testNow; }
constexpr int HIGH=1, LOW=0, INPUT=0, OUTPUT=1;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
class Print {
public:
    template<class... T> void printf(const char*, T...) {}
    void println(const char* = "") {}
};
inline Print Serial;
class Stream : public Print {
    unsigned long timeout = 1000;
public:
    virtual ~Stream() = default;
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t*, size_t) = 0;
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() = 0;
    void setTimeout(unsigned long ms) { timeout=ms; }
    unsigned long getTimeout() const { return timeout; }
    size_t readBytes(uint8_t* dest, size_t count) {
        size_t i=0;
        for (; i<count; ++i) {
            const auto started = testNow;
            int b=read();
            while (b<0 && testNow-started<timeout) {
                ++testNow;
                b=read();
            }
            if (b<0) break;
            dest[i]=b;
        }
        return i;
    }
};
class HardwareSerial : public Stream {
public:
    std::vector<uint8_t> incoming;
    size_t pos=0, writes=0;
    uint32_t responseDelayMs=0, readyAt=0;
    bool truncated=false, wrongAddress=false, oversized=false;
    void begin(unsigned long, uint32_t, int, int) {}
    void end() {}
    size_t write(uint8_t b) override { return write(&b,1); }
    size_t write(const uint8_t* request, size_t count) override {
        ++writes;
        readyAt=testNow+responseDelayMs;
        const size_t length=request[4]==1 ? 30 : 18;
        incoming.assign(length,0); pos=0;
        memcpy(incoming.data(),request,4);
        if (wrongAddress) incoming[0]^=1;
        incoming[4]=request[4]; incoming[5]=oversized ? 65 : length;
        if (request[4]==1) {
            float volume=100, flow=0.125;
            memcpy(incoming.data()+6,&volume,4);
            memcpy(incoming.data()+14,&flow,4);
        } else { incoming[6]=0x10; incoming[7]=0x0e; }
        incoming[length-4]=request[count-4]; incoming[length-3]=request[count-3];
        uint16_t crc=0xffff;
        for(size_t i=0;i<length-2;++i) {
            crc^=incoming[i];
            for(int j=0;j<8;++j) crc=(crc&1)?(crc>>1)^0xa001:crc>>1;
        }
        incoming[length-2]=crc&255; incoming[length-1]=crc>>8;
        if(truncated) incoming.resize(8);
        return count;
    }
    int available() override { return testNow>=readyAt ? incoming.size()-pos : 0; }
    int read() override { return testNow>=readyAt && pos<incoming.size()?incoming[pos++]:-1; }
    int peek() override { return testNow>=readyAt && pos<incoming.size()?incoming[pos]:-1; }
    void flush() override {}
};
