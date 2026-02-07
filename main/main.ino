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
#include "hwi_streams/rs485_stream.h"
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
// constexpr Source::SourceType COLD_TYPE = Source::SourceType::Test;
// constexpr Source::SourceType HOT_TYPE  = Source::SourceType::Test;
// constexpr uint32_t COLD_POOL_INTERVAL = 3000; // Интервал опроса для холодного канала (мс) Test
// constexpr uint32_t HOT_POOL_INTERVAL  = 3000; // Интервал опроса для горячего канала (мс) Test

constexpr uint32_t HEARTBEAT_INTERVAL = 60000 * 60; // Интервал отправки heartbeat в HA (мс)

constexpr uint32_t COLD_POOL_INTERVAL = 60000 * 5; // Интервал опроса для холодного канала (мс)
constexpr uint32_t HOT_POOL_INTERVAL  = 60000 * 5; // Интервал опроса для горячего канала (мс)

constexpr Source::SourceType COLD_TYPE = Source::SourceType::Smart;
constexpr Source::SourceType HOT_TYPE = Source::SourceType::Smart;

constexpr Driver::MeterModel COLD_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;
constexpr Driver::MeterModel HOT_DRV_MODEL = Driver::MeterModel::Pulsar_Du_15_20;

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
        Serial.println("Zigbee: Entering sleep mode...");
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        auto *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        Utils::setLed(0, 30, 30); // Бирюзовая вспышка на команду из HA
        for (auto ep : Zigbee.ep_objects) {
            if (msg->info.dst_endpoint == ep->getEndpoint()) {
                static_cast<ZigbeeWaterMeter*>(ep)->handleAttributeWrite(msg);
                break;
            }
        }
    }
    return ESP_OK;
}

/* --- ПРЕРЫВАНИЯ (Только для PulseSource) --- */
void IRAM_ATTR isr_cold() { if(coldSrc) static_cast<Source::PulseSource*>(coldSrc)->increment(); }
void IRAM_ATTR isr_hot()  { if(hotSrc)  static_cast<Source::PulseSource*>(hotSrc)->increment(); }


void saveConfiguration() {
    Serial.println("System: Writing configuration to Flash...");
    
    auto cs = zigbeeCold.get_serial();
    auto co = zigbeeCold.get_offset();
    auto hs = zigbeeHot.get_serial();
    auto ho = zigbeeHot.get_offset();  

    Serial.printf("Config to save -> Cold SN:%lu, Cold Off:%ld, Hot SN:%lu, Hot Off:%ld\n", cs, co, hs, ho);

    // Сохраняем настройки холодного канала
    prefs.putUInt("sc", cs);
    prefs.putInt("oc", co);
    
    // Сохраняем настройки горячего канала
    prefs.putUInt("sh", hs);
    prefs.putInt("oh", ho);
    
    // Заодно сохраним текущие литры (так как мы уже открыли NVS на запись)
    prefs.putULong64("cl", zigbeeCold.get_val());
    prefs.putULong64("hl", zigbeeHot.get_val());
}

void setup() {
    initHardware();    // Слой 0: Железо и питание
    loadSystemData();  // Слой 1: Память (NVS)
    initSources();     // Слой 2: Драйверы и Источники
    setupZigbee();     // Слой 3: Сетевой стек
    
    Serial.println("--- System initialized and running ---");
    Utils::flashLed(0, 30, 0, 1000); // Финальный зеленый сигнал
}

void initHardware() {
    Serial.begin(115200);
    Utils::setLed(30, 0, 0); // Статус: Загрузка
    
    // Питание и сон
    esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true };
    esp_pm_configure(&pm_config);

    // Шина данных
    if constexpr (NEED_RS485) {
        rs485Bus = new RS485Stream(&Serial1, RS485_EN);
        rs485Bus->begin(RS485_BAUD, RS485_CONFIG, RS485_RX, RS485_TX);
        rs485Bus->setTimeout(300);
    }
    
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
}

void loadSystemData() {
    prefs.begin("water", false);
    // Просто открываем хранилище. Чтение данных сделаем в initSources для локализации.
}

void initSources() {
    // 1. Читаем настройки из памяти
    uint32_t c_sn  = prefs.getUInt("sc", 0);
    uint32_t h_sn  = prefs.getUInt("sh", 0);
    uint64_t c_lit = prefs.getULong64("cl", 0);
    uint64_t h_lit = prefs.getULong64("hl", 0);
    int32_t  c_off = prefs.getInt("oc", 0);
    int32_t  h_off = prefs.getInt("oh", 0);
    
// Loaded config -> Cold SN:10128442, Cold Off:0, Hot SN:10128939, Hot Off:0

    Serial.printf("Loaded config -> Cold SN:%lu, Cold Off:%ld, Hot SN:%lu, Hot Off:%ld\n", c_sn, c_off, h_sn, h_off);
    
    // 2. Создаем драйверы (Protocol Layer)
    if constexpr (COLD_TYPE == Source::SourceType::Smart) {
        coldDrv = Driver::DriverFactory::create(COLD_DRV_MODEL, rs485Bus, c_sn);
        if (coldDrv) coldDrv->setLogger(&Serial);
    } else {
        Serial.println("Cold driver not created");
    }

    if constexpr (HOT_TYPE == Source::SourceType::Smart) {
        hotDrv = Driver::DriverFactory::create(HOT_DRV_MODEL, rs485Bus, h_sn);
        if (hotDrv) hotDrv->setLogger(&Serial);
    } else {
        Serial.println("Hot driver not created");
    }

    // 3. Создаем источники (Logic Layer)
    coldSrc = Source::SourceFactory::create(COLD_TYPE, c_lit, PULSE_COLD_PIN, coldDrv);
    hotSrc  = Source::SourceFactory::create(HOT_TYPE,  h_lit,  PULSE_HOT_PIN,  hotDrv);

    // 4. Тонкая настройка (Offsets & Start)
    if (coldSrc) { 
        coldSrc->setPollInterval(COLD_POOL_INTERVAL);
        coldSrc->setOffset(c_off); 
        coldSrc->setSerialNumber(c_sn);
        coldSrc->begin(); 
    } else {
        Serial.println("Cold source not created");
    }
    if (hotSrc) { 
        hotSrc->setPollInterval(HOT_POOL_INTERVAL);
        hotSrc->setOffset(h_off); 
        hotSrc->setSerialNumber(h_sn);
        hotSrc->begin(); 
    } else {
        Serial.println("Hot source not created");   
    }
}

void setupZigbee() {
    // Привязываем источники к эндпоинтам
    zigbeeCold.setSource(coldSrc);
    zigbeeHot.setSource(hotSrc);

    // Настраиваем коллбэки сохранения
    zigbeeCold.onSettingsChanged(saveConfiguration);
    zigbeeHot.onSettingsChanged(saveConfiguration);

    // Регистрация эндпоинтов в стеке
    zigbeeCold.begin(); 
    zigbeeHot.begin();
    Zigbee.addEndpoint(&zigbeeCold); 
    Zigbee.addEndpoint(&zigbeeHot);

    // Идентификация устройства
    zigbeeCold.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    zigbeeHot.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    // Запуск стека
    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) {
        Serial.println("Zigbee: CRITICAL ERROR STARTING STACK");
    }

    // Финальные настройки стека
    esp_zb_sleep_enable(true);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER);
}

void loop() {
    // 1. Опрос физики (RS-485 / Pulse)
    updateSources();

    // 2. Сетевая логика (только если подключены)
    if (Zigbee.connected()) {
        handleZigbeeReporting();
        handleAutoSave();
    }

    // 3. UI и Сервис
    updateStatusIndication();
    checkServiceButton();

    delay(5); 
}

/* --- РЕАЛИЗАЦИЯ БЛОКОВ --- */

// 1. Обновление источников данных
void updateSources() {
    if (coldSrc) coldSrc->tick();
    if (hotSrc)  hotSrc->tick();
}

// 2. Умная отправка данных в Zigbee
void handleZigbeeReporting() {
    static uint32_t boot_time = millis();
    static uint32_t last_heartbeat = 0;
    static bool first_run = true;
    uint32_t now = millis();

    // Разовый репорт конфигурации (SN и Offset) при старте
    if (first_run && (now - boot_time > 5000)) {
        first_run = false;
        Serial.println("Zigbee: Reporting initial config...");
        zigbeeCold.reportConfig();
        delay(200);
        zigbeeHot.reportConfig();
        return;
    }

    // Проверяем, изменились ли данные у эндпоинтов
    bool coldNeeds = zigbeeCold.shouldReport();
    bool hotNeeds  = zigbeeHot.shouldReport();
    
    // Heartbeat: принудительный репорт раз в 5 минут, даже если нет расхода
    bool timeout = (now - last_heartbeat >= HEARTBEAT_INTERVAL);

    if (coldSrc && coldSrc->hasHourChanged()) {
        zigbeeCold.reportHourly();
    }
    
    if (hotSrc && hotSrc->hasHourChanged()) {
        delay(100); 
        zigbeeHot.reportHourly();
    }

    if (coldNeeds || hotNeeds || timeout) {
        last_heartbeat = now;
        
        if (coldNeeds || timeout) {
            zigbeeCold.reportValue();
        }
        
        if (hotNeeds || timeout) {
            delay(100); 
            zigbeeHot.reportValue();
        }

        if (coldNeeds || hotNeeds) {
            Utils::setLed(30, 30, 30); // Короткая вспышка только на реальный расход
        }
    }

    // Репорт батарейки раз в час
    static uint32_t last_battery = 0;
    if (now - last_battery >= 3600000 || last_battery == 0) {
        last_battery = now;
        if (zigbeeCold.battery_supported()) zigbeeCold.reportBattery();
        if (zigbeeHot.battery_supported())  zigbeeHot.reportBattery();
    }
}

// 3. Автосохранение (NVS)
void handleAutoSave() {
    static uint32_t last_save = 0;
    if (millis() - last_save >= 900000) { 
        last_save = millis();
        saveConfiguration(); 
    }
}

// 4. Светодиод
void updateStatusIndication() {
    static uint32_t last_blink = 0;
    if (!Zigbee.connected()) {
        if (millis() - last_blink > 500) {
            last_blink = millis();
            static bool t = false; t = !t;
            t ? Utils::setLed(20, 20, 0) : Utils::setLed(0, 0, 0);
        }
    } else {
        Utils::setLed(0, 1, 0); // Heartbeat LED
    }
}

// 5. Кнопка
void checkServiceButton() {
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        uint32_t start = millis();
        while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            if (millis() - start > 3000) {
                Utils::flashLed(50, 0, 0, 1000);
                Zigbee.factoryReset();
                ESP.restart();
            }
            delay(10);
        }
    }
}