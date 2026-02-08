# ESP32-C6 Zigbee Water Meter

A professional, dual-channel Zigbee water meter firmware for ESP32-C6. Designed to interface with both smart RS485 meters (Modbus) and traditional pulse-output meters.

## Features

*   **Dual Channel Support:** Monitor Cold and Hot water usage simultaneously.
*   **Hybrid Input Modes:**
    *   **Smart Mode:** Reads digital data (Total Volume, Serial Number) via RS485 (currently supports [Pulsar Du 15/20](https://pulsarm.ru/products/schetchik-vody/kvartirnyy-schyetchik-vody-du-15-du-20/elektronnyy-schetchik-du15-rs-485-qn-1-5-m3-ch-l-110mm/)).
    *   **Pulse Mode:** Counts physical pulses from reed switches or open-collector outputs.
*   **Zigbee 3.0 End Device:**
    *   Low power consumption (Sleepy End Device).
    *   Reports Total Volume (m³) and Hourly Consumption.
    *   Configurable via Zigbee (Offset, Serial Number).
    *   Battery status reporting.
*   **Data Safety:**
    *   Auto-saves readings to NVS (Non-Volatile Storage) to survive power loss.
    *   Wear-leveling protection (saves every 15 mins or on config change).
*   **Enterprise Architecture:**
    *   Modular C++ design using Factory Pattern.
    *   Non-blocking event loop.
    *   Robust error handling and reconnection logic.

## Hardware

*   **Microcontroller:** ESP32-C6 (e.g., SuperMini C6).
*   **Communication:** RS485 Transceiver (MAX485/MAX3485) for Smart mode.
*   **Sensors:** Pulse meters or RS485 Modbus meters.

### Pinout Configuration

| Peripheral | GPIO Pin | Note |
| :--- | :--- | :--- |
| **RGB LED** | 8 | WS2812 / Neopixel |
| **Button** | 9 | Boot/Config Button |
| **RS485 RX** | 21 | |
| **RS485 TX** | 20 | |
| **RS485 EN** | 19 | DE/RE Pin |
| **Pulse Cold** | 10 | Interrupt Input |
| **Pulse Hot** | 11 | Interrupt Input |

## Installation

1.  **Environment:** PlatformIO or Arduino IDE with ESP32 Arduino Core (v3.0+).
2.  **Settings:**
    *   **Board:** ESP32-C6 (e.g., `esp32-c6-devkitc-1`).
    *   **Zigbee Mode:** End Device.
    *   **Partition Scheme:** Zigbee 8MB with SPIFFS.
3.  **Configuration:**
    Open `main/main.ino` and adjust the configuration section:
    ```cpp
    constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart; // or Pulse
    constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;
    ```
4.  **Flash:** Upload the firmware to your board.

## Zigbee Integration

### Pairing
1.  Hold the **BOOT button (GPIO 9)** for **3 seconds** until the LED flashes Red.
2.  The device will reset and enter pairing mode.
3.  The LED will flash Green/Yellow during connection attempts.
4.  Once connected, the LED will turn off (sleeping).

### Zigbee2MQTT
A custom converter is required to expose all features (Offsets, Serial Numbers, Hourly stats).
Copy `water_meter_converter.js` to your Zigbee2MQTT configuration folder and add it to `configuration.yaml`:

```yaml
external_converters:
  - water_meter_converter.js
```

### Attributes

| Cluster | Attribute ID | Name | Type | Access | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Metering** | 0x0000 | CurrentSummDelivered | u48 | R | Total Volume (m³) |
| **Metering** | 0x0400 | InstantaneousDemand | u32 | R | Last Hour Consumption (Liters) |
| **Metering** | 0x0100 | CurrentTier1SummDelivered | u48 | RW | Calibration Offset (Liters) |
| **Metering** | 0x0102 | CurrentTier2SummDelivered | u48 | RW | Meter Serial Number |

## Usage

### LED Status
*   **Cyan Flash:** Received command from Zigbee Coordinator.
*   **Green Flash:** Data transmitted successfully.
*   **Red Flash:** Connection lost or Reset triggered.
*   **Yellow Blink:** Searching for network.

### Button
*   **Long Press (>3s):** Factory Reset and Re-pairing.

## License

Copyright 2026 Andrey Nemenko.