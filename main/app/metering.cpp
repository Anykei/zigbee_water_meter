// Copyright 2026 Andrey Nemenko.
#ifndef RS485_UART_BRIDGE
#include "config.h"
#include "metering.h"
#include "state.h"
#include "zigbee_sleep.h"
#include "hwi_streams/rs485_stream.h"
#include <algorithm>

namespace App {

namespace {
std::unique_ptr<RS485Stream> rs485Bus;
std::unique_ptr<Driver::SmartMeterDriver> coldDrv;
std::unique_ptr<Driver::SmartMeterDriver> hotDrv;
bool g_rs485_power_on = false;
bool g_rs485_bus_started = false;

void setRs485Power(bool on) {
    if constexpr (!HAS_RS485_POWER_CONTROL) return;

    if (g_rs485_power_on == on) return;
    const bool level = RS485_POWER_ACTIVE_LOW ? !on : on;
    digitalWrite(RS485_POWER_PIN, level ? HIGH : LOW);
    g_rs485_power_on = on;
    Serial.printf("RS485: power %s on GPIO%d\n", on ? "ON" : "OFF", RS485_POWER_PIN);
}

void beginRs485Bus() {
    if (!rs485Bus || g_rs485_bus_started) return;

    rs485Bus->begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
    rs485Bus->setTimeout(RS485Stream::kResponseTimeoutMs);
    g_rs485_bus_started = true;
}

void endRs485Bus() {
    if (!rs485Bus || !g_rs485_bus_started) return;

    rs485Bus->flush();
    rs485Bus->end();
    g_rs485_bus_started = false;

    if constexpr (RS485_TX >= 0) pinMode(RS485_TX, INPUT);
    if constexpr (RS485_RX >= 0) pinMode(RS485_RX, INPUT);
    if constexpr (RS485_EN >= 0) pinMode(RS485_EN, INPUT);
}

void enableRs485ForPoll() {
    if constexpr (HAS_RS485_POWER_CONTROL) {
        const bool wasOn = g_rs485_power_on;
        setRs485Power(true);
        if (!wasOn) delay(RS485_POWER_SETTLE_MS);
    }
    beginRs485Bus();
}

void disableRs485AfterPoll() {
    if constexpr (!HAS_RS485_POWER_CONTROL) return;

    endRs485Bus();
    setRs485Power(false);
}

uint32_t millisUntilNextRs485Poll(uint32_t now) {
    uint32_t nextDelay = UINT32_MAX;

    if constexpr (COLD_USES_RS485) {
        if (coldSrc) nextDelay = std::min(nextDelay, coldSrc->millisUntilPoll(now));
    }
    if constexpr (HOT_USES_RS485) {
        if (hotSrc) nextDelay = std::min(nextDelay, hotSrc->millisUntilPoll(now));
    }

    return nextDelay;
}

bool shouldPowerDownRs485(uint32_t now) {
    if constexpr (!HAS_RS485_POWER_CONTROL) return false;
    if constexpr (RS485_POWER_IDLE_OFF_MS == 0) return true;

    const uint32_t nextDelay = millisUntilNextRs485Poll(now);
    return nextDelay == UINT32_MAX || nextDelay > RS485_POWER_IDLE_OFF_MS;
}

void IRAM_ATTR isr_cold() {
    if(coldSrc) {
        static_cast<Source::PulseSource*>(coldSrc.get())->increment();
        ZigbeeSleep::wakeFromISR();
    }
}

void IRAM_ATTR isr_hot() {
    if(hotSrc) {
        static_cast<Source::PulseSource*>(hotSrc.get())->increment();
        ZigbeeSleep::wakeFromISR();
    }
}

}  // namespace

void initMeterHardware() {
    if constexpr (NEED_RS485) {
        rs485Bus = std::make_unique<RS485Stream>(&Serial1, RS485_EN);

        if constexpr (HAS_RS485_POWER_CONTROL) {
            pinMode(RS485_POWER_PIN, OUTPUT);
            const bool offLevel = RS485_POWER_ACTIVE_LOW ? true : false;
            digitalWrite(RS485_POWER_PIN, offLevel ? HIGH : LOW);
            g_rs485_power_on = false;
            endRs485Bus();
        }

#if RS485_TX_GPIO_TEST_MS > 0
        enableRs485ForPoll();
        Serial.printf("RS485: GPIO TX pin test on GPIO%d for %lu ms\n", RS485_TX, (unsigned long)RS485_TX_GPIO_TEST_MS);
        pinMode(RS485_TX, OUTPUT);
        const uint32_t gpioTestStart = millis();
        bool level = false;
        while (millis() - gpioTestStart < RS485_TX_GPIO_TEST_MS) {
            level = !level;
            digitalWrite(RS485_TX, level ? HIGH : LOW);
            delay(500);
        }
        digitalWrite(RS485_TX, HIGH);
#endif

        if constexpr (!HAS_RS485_POWER_CONTROL) {
            beginRs485Bus();
        }

        Serial.printf(
            "RS485: UART RX=%d TX=%d EN=%d power=%d power_control=%d active_low=%d idle_off=%lu ms baud=%lu\n",
            RS485_RX,
            RS485_TX,
            RS485_EN,
            RS485_POWER_PIN,
            RS485_POWER_CONTROL_ENABLED,
            RS485_POWER_ACTIVE_LOW,
            (unsigned long)RS485_POWER_IDLE_OFF_MS,
            (unsigned long)RS485_BAUD
        );

#if RS485_TX_TEST_MS > 0
        enableRs485ForPoll();
        Serial.printf("RS485: TX diagnostic burst for %lu ms\n", (unsigned long)RS485_TX_TEST_MS);
        const uint32_t txTestStart = millis();
        while (millis() - txTestStart < RS485_TX_TEST_MS) {
            rs485Bus->write((uint8_t)0x55);
            delay(2);
        }
        rs485Bus->flush();
#endif

#if RS485_TX_GPIO_TEST_MS > 0 || RS485_TX_TEST_MS > 0
        disableRs485AfterPoll();
#endif
    }
}

void initSources(const StoredSettings& settings) {
    uint32_t c_sn = settings.cold.serial;
    uint32_t h_sn = settings.hot.serial;
    uint64_t c_lit = settings.cold.liters;
    uint64_t h_lit = settings.hot.liters;
    int32_t c_off = settings.cold.offset;
    int32_t h_off = settings.hot.offset;
    uint32_t c_poll_min = settings.cold.pollMinutes;
    uint32_t h_poll_min = settings.hot.pollMinutes;

#if DEFAULT_COLD_SERIAL > 0
    if (c_sn == 0) {
        c_sn = DEFAULT_COLD_SERIAL;
        Serial.printf("Using build default cold serial: %lu\n", (unsigned long)c_sn);
    }
#endif
#if DEFAULT_HOT_SERIAL > 0
    if (h_sn == 0) {
        h_sn = DEFAULT_HOT_SERIAL;
        Serial.printf("Using build default hot serial: %lu\n", (unsigned long)h_sn);
    }
#endif

    Serial.printf(
        "Loaded config -> Cold SN:%lu, Cold Off:%ld, Cold Poll:%lu min, Hot SN:%lu, Hot Off:%ld, Hot Poll:%lu min\n",
        c_sn,
        c_off,
        (unsigned long)(c_poll_min > 0 ? c_poll_min : (COLD_POOL_INTERVAL + Source::kMsPerMinute - 1) / Source::kMsPerMinute),
        h_sn,
        h_off,
        (unsigned long)(h_poll_min > 0 ? h_poll_min : (HOT_POOL_INTERVAL + Source::kMsPerMinute - 1) / Source::kMsPerMinute)
    );

    if constexpr (COLD_TYPE == Source::SourceType::Smart) {
        coldDrv.reset(Driver::DriverFactory::create(COLD_DRV_MODEL, rs485Bus.get(), c_sn));
        if (coldDrv) coldDrv->setLogger(&Serial);
    } else {
        Serial.println("Cold driver not created");
    }

    if constexpr (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv.reset(Driver::DriverFactory::create(HOT_DRV_MODEL, rs485Bus.get(), h_sn));
        if (hotDrv) hotDrv->setLogger(&Serial);
    } else {
        Serial.println("Hot driver not created");
    }

    coldSrc.reset(Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv.get()));
    hotSrc.reset(Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv.get()));

    if (coldSrc) {
        if (c_poll_min > 0) {
            coldSrc->setPollIntervalMinutes(c_poll_min);
        } else {
            coldSrc->setPollInterval(COLD_POOL_INTERVAL);
        }
        coldSrc->setOffset(c_off);
        coldSrc->setTestMode(kEnableTestIntervals);
        coldSrc->setSerialNumber(c_sn);
        coldSrc->begin();
        if constexpr (COLD_TYPE == Source::SourceType::Pulse) {
            attachInterrupt(digitalPinToInterrupt(PULSE_COLD_PIN), isr_cold, FALLING);
        }
    } else {
        Serial.println("Cold source not created");
    }
    if (hotSrc) {
        if (h_poll_min > 0) {
            hotSrc->setPollIntervalMinutes(h_poll_min);
        } else {
            hotSrc->setPollInterval(HOT_POOL_INTERVAL);
        }
        hotSrc->setOffset(h_off);
        hotSrc->setTestMode(kEnableTestIntervals);
        hotSrc->setSerialNumber(h_sn);
        hotSrc->begin();
        if constexpr (HOT_TYPE == Source::SourceType::Pulse) {
            attachInterrupt(digitalPinToInterrupt(PULSE_HOT_PIN), isr_hot, FALLING);
        }
    } else {
        Serial.println("Hot source not created");
    }
}

void updateSources() {
    const uint32_t now = millis();
    bool rs485EnabledForPoll = false;

    if constexpr (HAS_RS485_POWER_CONTROL) {
        bool rs485PollDue = false;
        if constexpr (COLD_USES_RS485) {
            rs485PollDue = rs485PollDue || (coldSrc && coldSrc->isPollDue(now));
        }
        if constexpr (HOT_USES_RS485) {
            rs485PollDue = rs485PollDue || (hotSrc && hotSrc->isPollDue(now));
        }

        if (rs485PollDue) {
            enableRs485ForPoll();
            rs485EnabledForPoll = true;
        }
    }

    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();

    if (rs485EnabledForPoll) {
        const uint32_t afterPollNow = millis();
        const uint32_t nextDelay = millisUntilNextRs485Poll(afterPollNow);

        if (shouldPowerDownRs485(afterPollNow)) {
            if (nextDelay == UINT32_MAX) {
                Serial.println("RS485: no pending polls; powering down.");
            } else {
                Serial.printf(
                    "RS485: next poll in %lu ms exceeds idle-off threshold %lu ms; powering down.\n",
                    (unsigned long)nextDelay,
                    (unsigned long)RS485_POWER_IDLE_OFF_MS
                );
            }
            disableRs485AfterPoll();
        } else {
            Serial.printf(
                "RS485: keeping power ON, next poll in %lu ms (idle-off threshold %lu ms).\n",
                (unsigned long)nextDelay,
                (unsigned long)RS485_POWER_IDLE_OFF_MS
            );
        }
    }
}

}  // namespace App
#endif  // RS485_UART_BRIDGE
