#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

#ifndef RGB_LED_PIN
#define RGB_LED_PIN -1
#endif

#ifndef GPIO_STATUS_LED_PIN
  #if defined(LED_BUILTIN)
    #define GPIO_STATUS_LED_PIN LED_BUILTIN
  #else
    #define GPIO_STATUS_LED_PIN -1
  #endif
#endif

#ifndef GPIO_STATUS_LED_ACTIVE_LOW
#define GPIO_STATUS_LED_ACTIVE_LOW 0
#endif

namespace Utils {

// Universal LED helpers — drive both RGB and GPIO LEDs simultaneously
// when both are configured. Each is independently enabled via pin >= 0.

constexpr bool hasRgbLed  = (RGB_LED_PIN >= 0);
constexpr bool hasGpioLed = (GPIO_STATUS_LED_PIN >= 0);

inline void initGpioLed() {
    if constexpr (hasGpioLed) {
        static bool initialized = false;
        if (!initialized) {
            pinMode(GPIO_STATUS_LED_PIN, OUTPUT);
            initialized = true;
        }
    }
}

inline void setLed(uint8_t r, uint8_t g, uint8_t b) {
    if constexpr (hasRgbLed) {
        rgbLedWrite(RGB_LED_PIN, r, g, b);
    }
    if constexpr (hasGpioLed) {
        initGpioLed();
        const bool on = (r > 0) || (g > 0) || (b > 0);
        const bool level = GPIO_STATUS_LED_ACTIVE_LOW ? !on : on;
        digitalWrite(GPIO_STATUS_LED_PIN, level ? HIGH : LOW);
    }
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
