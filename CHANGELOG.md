# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- Release the Nano RGB LED's RMT power lock after transmission to allow light sleep.
- Preserve the hardware-verified 1000 ms RS485 response timeout, with a delayed-response regression check.
- Preserve Arduino Zigbee callback dispatch by handling writes through endpoint overrides.
- Keep hourly reports ahead of heartbeat/on-change reports and handle timer rollover.
- Anchor smart-meter hour/day accounting to the first successful reading after boot.
- Apply RS485 timeouts to the wrapper and read complete frames without waiting for unused buffer space.
- Express Zigbee scheduler sleep thresholds in milliseconds and clarify that automatic deep sleep is not implemented.

### Added
- Automatic Zigbee light sleep using rebuilt ESP-IDF power management, a 20 ms idle threshold and 3-second parent polling.
- A system-signal wrapper preserving Arduino commissioning/rejoin/leave handling, plus awake locks for interview and peripheral work.
- GPIO/task wakeups for the service button and wakeups on coordinator writes; pulse builds retain awake interrupt counting.
- Light-sleep wakeup/duration diagnostics and host tests for sleep gating and signal forwarding.
- Pinned pioarduino 55.03.36 build with regenerated SDK settings and matching linker sections for power management.
- Host regression checks for accounting, reporting, and RS485, run in CI.

### Changed
- Split application configuration, metering, NVS storage, Zigbee reporting, board service and RS485 bridge code out of `main.cpp` while preserving startup and loop order.

## [0.1.0] - 2026-06-12

### Added
- System power Zigbee endpoint with Power Config reporting.
- ADC battery measurement path with fake calibration placeholders.
- Optional ADC divider enable pin support.
- Averaged ADC sampling for battery voltage.
- Configurable battery percentage curve points.
- Low-battery diagnostic logging.
- Board-specific PlatformIO environments for NanoESP32-C6 and Seeed XIAO ESP32C6.
- GitHub Actions CI builds for all PlatformIO environments.
- Release workflow for tagged firmware builds (`vX.Y.Z`).
- `VERSION` file as the single source for the next firmware release version.
- Version metadata generator for CI builds and local release checks.
- Changelog release preparation mode in the version metadata generator.
- Release helper script for preparing changelog, fallback version header, commit, tag, and push steps.

### Changed
- Production system battery reporting interval changed from 30 minutes to 6 hours.
- RS485 power-up now includes a configurable settle delay.
- Status LED helper now uses `rgbLedWrite()` instead of deprecated `neopixelWrite()`.
- XIAO test environment now extends the XIAO production environment.
- README updated to match the current PlatformIO-based project layout and CI/CD flow.
- CI now generates firmware version metadata through `scripts/generate_version.py`.
- CI now publishes Zigbee2MQTT converter files as release artifacts.
- Running `scripts/generate_version.py` no longer writes `include/version.h` unless `--header` is provided.

### Fixed
- Zigbee write callbacks now route only to water meter endpoints and no longer cast the system power endpoint to `ZigbeeWaterMeter`.
- LED helper header now has local fallback defaults for LED-related build flags.
- PlatformIO board overrides no longer redefine shared RS485 and ADC build flags.
- GitHub Actions workflow no longer creates releases from ordinary pushes or pull requests.
- Tag releases now require a matching `CHANGELOG.md` section and use it as the GitHub release body.
- Committed `include/version.h` is now a local development fallback; CI overwrites it from `VERSION` and Git metadata.
- CI now checks `water_meter_converter_en.js` and `water_meter_converter_ru.js` instead of the removed legacy converter file.
- Release notes no longer contain mojibake text and list artifacts dynamically.

## [0.0.5] - 2026-02-09

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

## Planned
- Additional meter driver support (beyond Pulsar)
- OTA firmware update support
- Extended battery optimization modes
- Multi-language documentation
