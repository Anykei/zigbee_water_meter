#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

namespace Utils {

// Universal LED helpers:
// 1) RGB LED via neopixelWrite(RGB_LED_PIN, r, g, b)
// 2) Single-color LED via GPIO_STATUS_LED_PIN (ON if any r/g/b > 0)
// 3) No-op if neither LED is configured

#if defined(RGB_LED_PIN) && (RGB_LED_PIN >= 0)

inline void setLed(uint8_t r, uint8_t g, uint8_t b) { 
    neopixelWrite(RGB_LED_PIN, r, g, b); 
}

#elif defined(GPIO_STATUS_LED_PIN) && (GPIO_STATUS_LED_PIN >= 0)

#ifndef GPIO_STATUS_LED_ACTIVE_LOW
#define GPIO_STATUS_LED_ACTIVE_LOW 0
#endif

inline void setLed(uint8_t r, uint8_t g, uint8_t b) {
    static bool initialized = false;
    if (!initialized) {
        pinMode(GPIO_STATUS_LED_PIN, OUTPUT);
        initialized = true;
    }

    const bool on = (r > 0) || (g > 0) || (b > 0);
    const bool level = GPIO_STATUS_LED_ACTIVE_LOW ? !on : on;
    digitalWrite(GPIO_STATUS_LED_PIN, level ? HIGH : LOW);
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

#else

inline void setLed(uint8_t, uint8_t, uint8_t) {}
inline void flashLed(uint8_t, uint8_t, uint8_t, uint16_t) {}
inline void showSystemStatus(bool) {}

#endif

} 

#endif