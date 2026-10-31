// Copyright 2026 Andrey Nemenko.
#ifdef RS485_UART_BRIDGE
#include "rs485_bridge.h"
#include <Arduino.h>

#ifndef RS485_RX
#define RS485_RX 21
#endif
#ifndef RS485_TX
#define RS485_TX 20
#endif
#ifndef RS485_EN
#define RS485_EN -1
#endif
#ifndef RS485_BAUD
#define RS485_BAUD 9600
#endif
#ifndef RS485_POWER_PIN
#define RS485_POWER_PIN -1
#endif
#ifndef RS485_POWER_CONTROL_ENABLED
#define RS485_POWER_CONTROL_ENABLED 0
#endif
#ifndef RS485_POWER_ACTIVE_LOW
#define RS485_POWER_ACTIVE_LOW 0
#endif
#ifndef RS485_POWER_SETTLE_MS
#define RS485_POWER_SETTLE_MS 100
#endif
#ifndef RS485_BRIDGE_USB_BAUD
#define RS485_BRIDGE_USB_BAUD 9600
#endif
#ifndef RS485_BRIDGE_BUFFER_SIZE
#define RS485_BRIDGE_BUFFER_SIZE 128
#endif
#ifndef RS485_BRIDGE_FRAME_GAP_US
#define RS485_BRIDGE_FRAME_GAP_US 3000
#endif

#define RS485_CONFIG SERIAL_8N1

namespace RS485Bridge {
namespace {
constexpr bool kBridgeHasPowerControl = RS485_POWER_CONTROL_ENABLED && (RS485_POWER_PIN >= 0);
constexpr bool kBridgeHasDirectionPin = RS485_EN >= 0;

void setBridgePower(bool on) {
    if constexpr (!kBridgeHasPowerControl) return;

    const bool level = RS485_POWER_ACTIVE_LOW ? !on : on;
    digitalWrite(RS485_POWER_PIN, level ? HIGH : LOW);
}

void setBridgeTransmit(bool on) {
    if constexpr (!kBridgeHasDirectionPin) return;

    digitalWrite(RS485_EN, on ? HIGH : LOW);
}

void writeRs485(const uint8_t *buffer, size_t size) {
    if (size == 0) return;

    setBridgeTransmit(true);
    Serial1.write(buffer, size);
    Serial1.flush();
    setBridgeTransmit(false);
}


void pumpUsbToRs485() {
    static uint8_t buffer[RS485_BRIDGE_BUFFER_SIZE];
    static size_t size = 0;
    static uint32_t lastByteAt = 0;

    while (Serial.available() > 0) {
        if (size >= sizeof(buffer)) {
            writeRs485(buffer, size);
            size = 0;
        }
        buffer[size++] = static_cast<uint8_t>(Serial.read());
        lastByteAt = micros();
    }

    if (size > 0 && static_cast<uint32_t>(micros() - lastByteAt) >= RS485_BRIDGE_FRAME_GAP_US) {
        writeRs485(buffer, size);
        size = 0;
    }
}

void pumpRs485ToUsb() {
    static uint8_t buffer[RS485_BRIDGE_BUFFER_SIZE];
    static size_t size = 0;
    static uint32_t lastByteAt = 0;

    while (Serial1.available() > 0) {
        if (size >= sizeof(buffer)) {
            Serial.write(buffer, size);
            size = 0;
        }
        buffer[size++] = static_cast<uint8_t>(Serial1.read());
        lastByteAt = micros();
    }

    if (size > 0 && static_cast<uint32_t>(micros() - lastByteAt) >= RS485_BRIDGE_FRAME_GAP_US) {
        Serial.write(buffer, size);
        Serial.flush();
        size = 0;
    }
}

}  // namespace

void setup() {
    Serial.begin(RS485_BRIDGE_USB_BAUD);

    if constexpr (kBridgeHasPowerControl) {
        pinMode(RS485_POWER_PIN, OUTPUT);
        setBridgePower(true);
        delay(RS485_POWER_SETTLE_MS);
    }

    if constexpr (kBridgeHasDirectionPin) {
        pinMode(RS485_EN, OUTPUT);
        setBridgeTransmit(false);
    }

    Serial1.begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
}

void loop() {
    pumpUsbToRs485();
    pumpRs485ToUsb();
}
}  // namespace RS485Bridge
#endif
