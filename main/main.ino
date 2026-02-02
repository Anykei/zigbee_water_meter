/*
 * Copyright (C) 2026 Andrey Nemenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_pm.h"
#include <Preferences.h>

/* --- Конфигурация --- */
#define MODEL_ID "C6_WATER_V2"
#define MANUFACTURER_NAME "MuseLab"
#define RGB_LED_PIN 8
#define BOOT_BUTTON_PIN 9 
#define PULSE_COLD_PIN 10
#define PULSE_HOT_PIN 11
#define TX_POWER 20
#define SIMULATION_MODE 

/* --- Глобальные объекты --- */
Preferences prefs;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

/* --- Утилиты индикации --- */
void setLed(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED_PIN, r, g, b); }

void flashLed(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    setLed(r, g, b);
    delay(ms);
    setLed(0, 0, 0);
}

/* --- КЛАСС УСТРОЙСТВА --- */
class ZigbeeWaterMeter : public ZigbeeEP {
private:
    bool with_battery;
    volatile bool needs_immediate_report;
    uint8_t battery_level;
    
    alignas(8) uint64_t value; // Литры (Raw)
    alignas(4) uint32_t multiplier = 1;
    alignas(4) uint32_t divisor = 1000;
    alignas(4) int32_t offset_value = 0;
    alignas(4) uint32_t serial_number = 0;
    
public:
    ZigbeeWaterMeter(uint8_t endpoint, bool with_battery = false) : 
        ZigbeeEP(endpoint), value(0), battery_level(100), needs_immediate_report(false) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
        this->with_battery = with_battery;
    }

    // Потокобезопасные методы доступа
    void set_val(uint64_t v) { portENTER_CRITICAL(&mux); this->value = v; portEXIT_CRITICAL(&mux); }
    uint64_t get_val() { uint64_t v; portENTER_CRITICAL(&mux); v = this->value; portEXIT_CRITICAL(&mux); return v; }
    
    void set_offset(int32_t v) { this->offset_value = v; }
    void set_serial(uint32_t v) { this->serial_number = v; }
    void set_battery(uint8_t v) { this->battery_level = v; }

    void increment() {
        portENTER_CRITICAL_ISR(&mux);
        this->value++;
        this->needs_immediate_report = true;
        portEXIT_CRITICAL_ISR(&mux);
    }

    bool check_needs_report() { return needs_immediate_report; }
    void clear_report_flag() { needs_immediate_report = false; }

    void begin() {
        _cluster_list = esp_zb_zcl_cluster_list_create();
        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x03 };
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        if (this->with_battery) {
            uint8_t battery_perc = 200; 
            esp_zb_attribute_list_t *p_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
            esp_zb_cluster_add_attr(p_attr, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021, 0x20, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &battery_perc);
            esp_zb_cluster_list_add_power_config_cluster(_cluster_list, p_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        }

        esp_zb_attribute_list_t *m_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);
        uint8_t def_u48[6] = {0};
        uint8_t uom = 0x07; uint8_t fmt = 0x4B; uint8_t type = 0x02;
        
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0000, 0x25, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0200, 0x2b, 0x01 | 0x02 | 0x08, &this->offset_value);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0201, 0x23, 0x01 | 0x02 | 0x08, &this->serial_number);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0300, 0x30, 0x01, &uom);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0303, 0x18, 0x01, &fmt);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0306, 0x30, 0x01, &type);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0301, 0x22, 0x01, &this->multiplier);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0302, 0x22, 0x01, &this->divisor);

        esp_zb_cluster_list_add_metering_cluster(_cluster_list, m_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    void reportBattery() {
        if (!this->with_battery) return;
        uint8_t zb_val = this->battery_level * 2; // 50% -> 100

        esp_zb_lock_acquire(portMAX_DELAY);
        
        esp_zb_zcl_set_attribute_val(
            _endpoint, 
            ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 
            0x0021, 
            &zb_val, 
            false
        );
        
        esp_zb_zcl_report_attr_cmd_t report_cmd;
        memset(&report_cmd, 0, sizeof(report_cmd));
        report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        report_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG;
        report_cmd.attributeID = 0x0021; // И здесь тоже 0x0021
        report_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        report_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
        report_cmd.zcl_basic_cmd.dst_endpoint = 1; 
        report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
        
        esp_zb_zcl_report_attr_cmd_req(&report_cmd);
        esp_zb_lock_release();
    }

    void reportValue() {
        uint64_t total = this->value + (int64_t)((uint32_t)this->offset_value);
        
        uint8_t zb_value[6];
        for (int i = 0; i < 6; i++) zb_value[i] = (total >> (i * 8)) & 0xFF;
        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, zb_value, false);
        esp_zb_zcl_report_attr_cmd_t report_cmd;
        memset(&report_cmd, 0, sizeof(report_cmd));
        report_cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        report_cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_METERING;
        report_cmd.attributeID = ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID;
        report_cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        report_cmd.zcl_basic_cmd.src_endpoint = _endpoint;
        report_cmd.zcl_basic_cmd.dst_endpoint = 1; 
        report_cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
        esp_zb_zcl_report_attr_cmd_req(&report_cmd);
        esp_zb_lock_release();
    }

    void handleAttributeWrite(const esp_zb_zcl_set_attr_value_message_t *message) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_METERING) {
            if (message->attribute.id == 0x0200) {
                this->offset_value = *(int32_t *)message->attribute.data.value;
                prefs.putInt(_endpoint == 1 ? "oc" : "oh", offset_value);
                this->needs_immediate_report = true;
                Serial.printf("EP %d Offset -> %ld\n", _endpoint, offset_value);
            }
            else if (message->attribute.id == 0x0201) {
                this->serial_number = *(uint32_t *)message->attribute.data.value;
                prefs.putUInt(_endpoint == 1 ? "sc" : "sh", serial_number);
                this->needs_immediate_report = true;
                Serial.printf("EP %d Serial -> %lu\n", _endpoint, serial_number);
            }
        }
    }
};

ZigbeeWaterMeter coldMeter(1, true); 
ZigbeeWaterMeter hotMeter(2, false);

/* --- Обработчик Dispatcher --- */
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
    
    if (callback_id == ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        Serial.println("Zigbee can sleep now");
        esp_zb_sleep_now();  // Теперь blocking sleep до wakeup
        return ESP_OK;
    }

    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID) {
        auto *msg = (esp_zb_zcl_set_attr_value_message_t *)message;
        setLed(0, 30, 30); // Бирюзовая вспышка на команду из HA
        for (auto ep : Zigbee.ep_objects) {
            if (msg->info.dst_endpoint == ep->getEndpoint()) {
                static_cast<ZigbeeWaterMeter*>(ep)->handleAttributeWrite(msg);
                break;
            }
        }
    }
    return ESP_OK;
}

/* --- ПРЕРЫВАНИЯ --- */
void IRAM_ATTR isr_cold() { coldMeter.increment(); }
void IRAM_ATTR isr_hot()  { hotMeter.increment(); }

void setup() {
    Serial.begin(115200);
    setLed(30, 0, 0); // Инициализация - Красный
    
    esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true };
    esp_pm_configure(&pm_config);

    prefs.begin("water", false);
    coldMeter.set_val(prefs.getULong64("c", 0));
    hotMeter.set_val(prefs.getULong64("h", 0));
    coldMeter.set_offset(prefs.getInt("oc", 0));
    coldMeter.set_serial(prefs.getUInt("sc", 0));
    hotMeter.set_offset(prefs.getInt("oh", 0));
    hotMeter.set_serial(prefs.getUInt("sh", 0));

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    pinMode(PULSE_COLD_PIN, INPUT_PULLUP);
    pinMode(PULSE_HOT_PIN, INPUT_PULLUP);
    attachInterrupt(PULSE_COLD_PIN, isr_cold, FALLING);
    attachInterrupt(PULSE_HOT_PIN, isr_hot, FALLING);

    coldMeter.begin(); hotMeter.begin();
    Zigbee.addEndpoint(&coldMeter); Zigbee.addEndpoint(&hotMeter);
    coldMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    hotMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    Serial.println("Zigbee starting...");
    if(!Zigbee.begin(ZIGBEE_END_DEVICE)) Serial.println("Zigbee Error");

    esp_zb_sleep_enable(true);
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_set_tx_power(TX_POWER); 
    
    Serial.println("System Ready");
    flashLed(0, 30, 0, 1000); // Система готова - Зеленый
}

void loop() {
    static uint32_t last_report = 0;
    static uint32_t last_battery = 0;
    static uint32_t last_blink = 0;
    static uint8_t fake_batt = 100;
    uint32_t now = millis();

    // 1. Индикация поиска сети
    if (!Zigbee.connected()) {
        if (now - last_blink > 500) {
            last_blink = now;
            static bool t = false; t = !t;
            t ? setLed(20, 20, 0) : setLed(0, 0, 0); // Мигающий желтый
        }
    } else {
        // Подключено - очень тусклый зеленый (режим работы)
        setLed(0, 1, 0); 
    }

    // 2. Обработка симуляции
    #ifdef SIMULATION_MODE
    static uint32_t last_sim = 0;
    if (now - last_sim > 1500) {
        last_sim = now;
        setLed(0, 0, 30); // Синяя вспышка на импульс
        coldMeter.increment();
        hotMeter.increment();
        delay(50);
        Serial.println("Data INC");
    }
    #endif

    // 3. Логика отчетов
    bool force = (coldMeter.check_needs_report() || hotMeter.check_needs_report());

    if (Zigbee.connected()) {
        if (force || (now - last_report > 60000)) { // Раз в минуту или по изменению
            last_report = now;
            
            prefs.putULong64("c", coldMeter.get_val());
            prefs.putULong64("h", hotMeter.get_val());
            
            setLed(30, 30, 30); // Белая вспышка на передачу
            coldMeter.reportValue();
            delay(200); 
            hotMeter.reportValue();
            
            coldMeter.clear_report_flag();
            hotMeter.clear_report_flag();
            Serial.println("Data reported");
        }

        // Батарейка раз в 10 минут (для теста)
        if (now - last_battery > 600000 || last_battery == 0) {
            last_battery = now;
            coldMeter.set_battery(fake_batt--);
            if (fake_batt < 10) fake_batt = 100;
            coldMeter.reportBattery();
        }
    }

    // 4. Сброс
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        delay(3000);
        if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
            flashLed(50, 0, 0, 1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    }
    delay(10);
}