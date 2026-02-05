/*
 * Copyright (C) 2026 Andrey Nemenko
 * Project: ESP32-C6 Zigbee Water Meter with Factory Pattern
 */

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_pm.h"
#include <Preferences.h>

// Слои абстракции
#include "utils.h"
#include "zigbee_water_meter.h"
#include "rs485_stream.h"
#include "drivers/driver_factory.h"
#include "sources/factory_source.h"

/* --- НАСТРОЙКИ ЖЕЛЕЗА --- */
#define RGB_LED_PIN      8
#define BOOT_BUTTON_PIN  9
#define RS485_RX         21
#define RS485_TX         20
#define RS485_EN         19
#define RS485_BAUD       9600
#define RS485_CONFIG     SERIAL_8N1
#define PULSE_COLD_PIN   10
#define PULSE_HOT_PIN    11

/* --- НАСТРОЙКИ ZIGBEE --- */
#define MODEL_ID "C6_WATER_METER"
#define MANUFACTURER_NAME "MuseLab"
#define TX_POWER 12

/* --- КОНФИГУРАЦИЯ (ВЫБОР ТИПА И ДРАЙВЕРА) --- */
// Для тестов можно поставить Source::SourceType::Test или Driver::MeterModel::Mock
constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_V2;

constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_V2;

constexpr bool NEED_RS485 = (COLD_TYPE == Source::SourceType::Smart || HOT_TYPE == Source::SourceType::Smart);

/* --- ГЛОБАЛЬНЫЕ ОБЪЕКТЫ --- */
Preferences prefs;
RS485Stream* rs485Bus = nullptr; 

// Эндпоинты Zigbee
ZigbeeWaterMeter zigbeeCold(1, true); 
ZigbeeWaterMeter zigbeeHot(2, false);

// Интерфейсы (Указатели)
Driver::SmartMeterDriver *coldDrv = nullptr;
Driver::SmartMeterDriver *hotDrv = nullptr;
Source::WaterSource *coldSrc = nullptr;
Source::WaterSource *hotSrc = nullptr;

/* --- ОБРАБОТЧИК СОБЫТИЙ ZIGBEE --- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    if (callback_id == ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        esp_zb_sleep_now();
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_CMD_WRITE_ATTR_CB_ID) {
        auto *msg = (esp_zb_zcl_write_attr_message_t *)message;
        Utils::setLed(0, 30, 30); // Бирюзовая вспышка
        
        uint8_t ep_id = msg->info.dst_endpoint;
        // Диспетчеризация по объектам
        if (ep_id == 1) zigbeeCold.handleAttributeWrite((esp_zb_zcl_set_attr_value_message_t*)msg);
        else if (ep_id == 2) zigbeeHot.handleAttributeWrite((esp_zb_zcl_set_attr_value_message_t*)msg);
    }
    return ESP_OK;
}

/* --- ПРЕРЫВАНИЯ (Только для PulseSource) --- */
void IRAM_ATTR isr_cold() { if(coldSrc) static_cast<Source::PulseSource*>(coldSrc)->increment(); }
void IRAM_ATTR isr_hot()  { if(hotSrc)  static_cast<Source::PulseSource*>(hotSrc)->increment(); }

void setup() {
    Serial.begin(115200);
    Utils::setLed(30, 0, 0); // Старт

    // --- УСЛОВНАЯ ИНИЦИАЛИЗАЦИЯ ЖЕЛЕЗА ---
        if constexpr (NEED_RS485) {
        Serial.println("System: Initializing RS-485 bus...");
        rs485Bus = new RS485Stream(&Serial1, RS485_EN);
        rs485Bus->begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
        rs485Bus->setTimeout(300);
        Serial.printf("RS-485 started at %d baud\n", RS485_BAUD);
    } else {
        Serial.println("System: RS-485 not required for this config.");
    }

    // 2. Сон и питание
    esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true };
    esp_pm_configure(&pm_config);

    // 3. Память и восстановление
    prefs.begin("water", false);
    zigbeeCold.setPreferences(&prefs);
    zigbeeHot.setPreferences(&prefs);

    uint64_t c_lit = prefs.getULong64("cl", 0);
    uint64_t h_lit = prefs.getULong64("hl", 0);
    uint32_t c_sn  = prefs.getUInt("sc", 10128939);
    uint32_t h_sn  = prefs.getUInt("sh", 10128442);

    // 4. Сборка через Фабрики
    if (COLD_TYPE == Source::SourceType::Smart) {
        coldDrv = Driver::DriverFactory::create(COLD_DRV_MODEL, &rs485Bus, c_sn);
        if (coldDrv) coldDrv->setLogger(&Serial);
    }
    if (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv = Driver::DriverFactory::create(HOT_DRV_MODEL, &rs485Bus, h_sn);
        if (hotDrv) hotDrv->setLogger(&Serial);
    }

    coldSrc = Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv);
    hotSrc  = Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv);

    // 5. Связи (Dependency Injection)
    zigbeeCold.setSource(coldSrc); 
    zigbeeHot.setSource(hotSrc);
    
    // Подписка на изменение SN из Zigbee (обновление адреса драйвера)
    zigbeeCold.onSerialChange([](uint32_t sn){ if(coldDrv) coldDrv->setAddress(sn); });
    zigbeeHot.onSerialChange([](uint32_t sn){ if(hotDrv) hotDrv->setAddress(sn); });

    // Инициализация значений эндпоинтов
    zigbeeCold.set_val(c_lit);
    zigbeeCold.set_offset(prefs.getInt("oc", 0));
    zigbeeCold.set_serial(c_sn);

    zigbeeHot.set_val(h_lit);
    zigbeeHot.set_offset(prefs.getInt("oh", 0));
    zigbeeHot.set_serial(h_sn);

    // 6. Инициализация источников и прерываний
    if (coldSrc) coldSrc->begin();
    if (hotSrc)  hotSrc->begin();
    if (COLD_TYPE == Source::SourceType::Pulse) attachInterrupt(PULSE_COLD_PIN, isr_cold, FALLING);
    if (HOT_TYPE == Source::SourceType::Pulse)  attachInterrupt(PULSE_HOT_PIN, isr_hot, FALLING);

    // 7. Запуск Zigbee
    zigbeeCold.begin(); zigbeeHot.begin();
    Zigbee.addEndpoint(&zigbeeCold); Zigbee.addEndpoint(&zigbeeHot);
    zigbeeCold.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    zigbeeHot.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) Serial.println("Zigbee Error");

    esp_zb_sleep_enable(true);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER); 
    
    Utils::flashLed(0, 30, 0, 1000); // Ready
    Serial.println("System Ready");
}

void loop() {
    static uint32_t last_report = 0;
    static uint32_t last_save = 0;
    static uint32_t last_battery = 0;
    uint32_t now = millis();

    // 1. Опрос источников (Абстрактный "тик")
    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();

    // Синхронизация: Источник -> Zigbee Интерфейс
    zigbeeCold.set_val(coldSrc->getLiters());
    zigbeeHot.set_val(hotSrc->getLiters());

    // 2. Индикация статуса
    if (!Zigbee.connected()) {
        static uint32_t last_blink = 0;
        if (now - last_blink > 500) {
            last_blink = now;
            static bool t = false; t = !t;
            t ? Utils::setLed(20, 20, 0) : Utils::setLed(0, 0, 0); // Желтый
        }
    } else {
        Utils::setLed(0, 1, 0); // Тускло-зеленый
    }

    // 3. Отчеты и обслуживание (только при наличии сети)
    if (Zigbee.connected()) {
        bool force = (zigbeeCold.check_needs_report() || zigbeeHot.check_needs_report());

        if (force || (now - last_report > 60000)) {
            last_report = now;
            Utils::setLed(30, 30, 30); // Белая вспышка
            zigbeeCold.reportValue();
            delay(100); 
            zigbeeHot.reportValue();
            zigbeeCold.clear_report_flag();
            zigbeeHot.clear_report_flag();
        }

        // Сохранение во Flash (раз в 10 минут)
        if (now - last_save > 600000) {
            last_save = now;
            prefs.putULong64("cl", zigbeeCold.get_val());
            prefs.putULong64("hl", zigbeeHot.get_val());
        }

        // Батарейка (раз в час)
        if (now - last_battery > 3600000 || last_battery == 0) {
            last_battery = now;
            // Можно добавить чтение реального вольтажа из драйвера через getValue
            zigbeeCold.reportBattery();
        }
    }

    // 4. Сервисная кнопка
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        delay(3000);
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            Utils::flashLed(50, 0, 0, 1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    }
    delay(10);
}