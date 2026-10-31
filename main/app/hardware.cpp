// Copyright 2026 Andrey Nemenko.
#ifndef RS485_UART_BRIDGE
#include "config.h"
#include "hardware.h"
#include "metering.h"
#include "Zigbee.h"
#include "zigbee_sleep.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_partition.h"

namespace App {

namespace {
void IRAM_ATTR isr_service_button() {
    // Level wakeups must not generate an interrupt storm while BOOT is held.
    gpio_intr_disable(static_cast<gpio_num_t>(BOOT_BUTTON_PIN));
    ZigbeeSleep::wakeFromISR();
}
}  // namespace

void initHardware() {
    Serial.begin(115200);
    StatusLedSet(30, 0, 0);

    initMeterHardware();

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOOT_BUTTON_PIN), isr_service_button, ONLOW);
    ESP_ERROR_CHECK(gpio_wakeup_enable(static_cast<gpio_num_t>(BOOT_BUTTON_PIN), GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
}

void checkBootRecovery() {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        Serial.println("\n!!! BOOT BUTTON HELD - RECOVERY MODE !!!");
        StatusLedSet(50, 0, 0);
        delay(3000);

        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            Serial.println("Erasing NVS...");
            nvs_flash_erase();
            nvs_flash_init();

            Serial.println("Erasing Zigbee Storage...");
            const esp_partition_t* zb_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "zb_storage");
            if (zb_part) {
                esp_partition_erase_range(zb_part, 0, zb_part->size);
                Serial.println("Done.");
            } else {
                Serial.println("Partition 'zb_storage' not found!");
            }

            StatusLedFlash(0, 50, 0, 1000);
            Serial.println("Restarting...");
            ESP.restart();
        }
    }
}

void checkServiceButton() {
    static uint32_t press_start = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (press_start == 0) {
            press_start = millis();
        } else if (millis() - press_start > 3000) {
            StatusLedFlash(50, 0, 0, 1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    } else {
        press_start = 0;
        // Re-arm only after release; gpio_wakeup_enable and the ISR both use LOW.
        gpio_intr_enable(static_cast<gpio_num_t>(BOOT_BUTTON_PIN));
    }
}

void updateStatusIndication() {
    static uint32_t last_blink = 0;
    if (!Zigbee.connected()) {
        if (millis() - last_blink > 500) {
            last_blink = millis();
            static bool t = false; t = !t;
            t ? StatusLedSet(20, 20, 0) : StatusLedSet(0, 0, 0);
        }
    } else {
        StatusLedSet(0, 0, 0);
    }
}

#ifdef TEST
void logBatteryAdcForTest() {
    static uint32_t last_adc_log = 0;
    uint32_t now = millis();
    if (now - last_adc_log < 5000) return;
    last_adc_log = now;

#if ADC_BATTERY_VOLTAGE_PIN < 0
    Serial.println("ADC Test: disabled because ADC_BATTERY_VOLTAGE_PIN < 0");
#else
    if constexpr (ADC_BATTERY_ENABLE_PIN >= 0) {
        const bool level = ADC_BATTERY_ENABLE_ACTIVE_LOW ? false : true;
        pinMode(ADC_BATTERY_ENABLE_PIN, OUTPUT);
        digitalWrite(ADC_BATTERY_ENABLE_PIN, level ? HIGH : LOW);
        if (ADC_BATTERY_POWER_SETTLE_MS > 0) delay(ADC_BATTERY_POWER_SETTLE_MS);
    }

    uint32_t total_mv = 0;
    const uint8_t samples = ADC_BATTERY_SAMPLES > 0 ? ADC_BATTERY_SAMPLES : 1;
    for (uint8_t i = 0; i < samples; ++i) {
        total_mv += analogReadMilliVolts(ADC_BATTERY_VOLTAGE_PIN);
        if (i + 1 < samples && ADC_BATTERY_SAMPLE_DELAY_MS > 0) {
            delay(ADC_BATTERY_SAMPLE_DELAY_MS);
        }
    }

    if constexpr (ADC_BATTERY_ENABLE_PIN >= 0) {
        const bool level = ADC_BATTERY_ENABLE_ACTIVE_LOW ? true : false;
        digitalWrite(ADC_BATTERY_ENABLE_PIN, level ? HIGH : LOW);
    }

    uint32_t raw_mv = total_mv / samples;
    int32_t calibrated_mv = (int32_t)((raw_mv * ADC_BATTERY_VOLTAGE_SCALE) + ADC_BATTERY_VOLTAGE_OFFSET_MV + 0.5f);
    if (calibrated_mv < 0) calibrated_mv = 0;

    Serial.printf(
        "ADC Test: pin=%d raw=%lu mV scale=%.3f battery=%ld mV samples=%u\n",
        ADC_BATTERY_VOLTAGE_PIN,
        (unsigned long)raw_mv,
        (double)ADC_BATTERY_VOLTAGE_SCALE,
        (long)calibrated_mv,
        samples
    );
#endif
}
#endif

}  // namespace App
#endif  // RS485_UART_BRIDGE
