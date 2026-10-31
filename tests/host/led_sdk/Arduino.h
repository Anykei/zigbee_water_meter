#pragma once
#include <cassert>
#include <cstdint>

constexpr int OUTPUT = 1, LOW = 0, HIGH = 1;
inline int rmtLocks = 0, writes = 0, releases = 0;
inline int gpioMode = -1, gpioLevel = -1;
inline uint8_t red = 0, green = 0, blue = 0;
inline void pinMode(int, int mode) { gpioMode = mode; }
inline void digitalWrite(int, int level) { gpioLevel = level; }
inline void delay(uint16_t) { assert(rmtLocks == 0); }
inline void rgbLedWrite(int, uint8_t r, uint8_t g, uint8_t b) {
    // Arduino returns after TX, with the channel and its PM lock still enabled.
    if (rmtLocks == 0) ++rmtLocks;
    ++writes;
    red = r; green = g; blue = b;
}
