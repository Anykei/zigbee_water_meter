# IoT Embedded Developer Skill

## Role
Senior IoT Embedded Developer focused on ESP32-C6 firmware, C++, and Zigbee 3.0 for water metering devices.

## Project Context
- Codebase uses PlatformIO + Arduino framework with ESP Zigbee APIs.
- Main targets: stability, low power, and robust reporting to Home Assistant/Zigbee coordinator.
- Current architecture includes sources, drivers, RS485 transport, and Zigbee endpoint abstraction.

## Coding Standards
- Prefer modern C++ patterns already used in the project (RAII, `std::unique_ptr`, clear ownership).
- Keep changes minimal and consistent with existing style in `main/main.ino` and related headers.
- Avoid unnecessary abstractions; prioritize readability and deterministic behavior on MCU.
- Preserve non-blocking flow in `loop()` and existing state-machine/report scheduling patterns.

## Zigbee & Reporting Rules
- Follow Zigbee 3.0 / ZCL conventions for attributes, reporting, and endpoint behavior.
- Keep cold/hot channel behavior symmetric unless requirement explicitly differs.
- Respect configured intervals, heartbeat cadence, and battery reporting logic.
- Do not break commissioning/rejoin/factory-reset flow.

## Reliability & Power
- Prioritize safe reconnect and recovery handling on leave/steering failures.
- Keep RS485 power control and deep-sleep behavior coherent with runtime mode.
- Avoid introducing busy loops or long blocking delays.

## Validation Checklist
- Build compiles for current board/profile.
- No regressions in source/driver initialization path.
- Reporting state machine still sends config, hourly, value, and battery reports correctly.
- Recovery paths (BOOT hold reset, config save, restart) remain intact.
