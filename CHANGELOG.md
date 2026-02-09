# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-02-09

### Added
- Initial production release
- Dual-channel water meter support (Cold & Hot)
- Smart Mode: RS485 Modbus support for Pulsar Du 15/20 meters
- Pulse Mode: Traditional pulse counter support
- Test Mode: Simulated data for development
- Zigbee 3.0 End Device implementation with deep sleep optimization
- Power consumption: ~21 mA average (5-minute polling intervals)
- Auto-save to NVS every 15 minutes with wear-leveling protection
- Emergency recovery mode via boot button
- Factory reset function (hold boot button >3s)
- Non-blocking state machine for Zigbee reporting
- Heartbeat reports every 30 minutes
- Battery status reporting every 30 minutes
- Hourly consumption tracking and reporting
- Configurable offset and serial number via Zigbee
- RGB LED status indicators
- Serial diagnostics at 115200 baud
- Sleep cycle tracking with diagnostic logging
- Zigbee2MQTT custom converter

### Architecture
- Modern C++17 with smart pointers and RAII
- Factory Pattern for drivers and sources
- Modular design with clear separation of concerns:
  - Driver Layer: RS485 communication protocols
  - Source Layer: Data acquisition logic
  - Zigbee Layer: Network and reporting
  - Hardware Layer: GPIO, NVS, peripherals
- Defensive programming with null pointer checks
- Google C++ Style Guide compliance

### Performance
- Loop execution: ~5ms per iteration (idle)
- RS485 transaction: 1-2 seconds per meter
- Zigbee report: ~100ms average
- NVS write: ~50ms
- Sleep cycle: ~120s between activity bursts

### Hardware Support
- ESP32-C6 SuperMini
- MAX485/MAX3485 RS485 transceivers
- WS2812 RGB LED status indicator
- Pulse counter inputs with interrupt support
- Configurable RS485 power control

## [Unreleased]

### Planned
- Additional meter driver support (beyond Pulsar)
- OTA firmware update support
- Extended battery optimization modes
- Multi-language documentation
