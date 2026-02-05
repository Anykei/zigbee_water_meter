#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

#ifndef RGB_LED_PIN
#define RGB_LED_PIN 8 // Дефолтный пин для SuperMini C6
#endif

namespace Utils {

inline void setLed(uint8_t r, uint8_t g, uint8_t b) { 
    neopixelWrite(RGB_LED_PIN, r, g, b); 
}

inline void flashLed(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    setLed(r, g, b);
    delay(ms);
    setLed(0, 0, 0);
}

inline void showSystemStatus(bool connected) {
    if (connected) {
        setLed(0, 2, 0); 
    } else {
        setLed(10, 10, 0); 
    }
}

} 

#endif