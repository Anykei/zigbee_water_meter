#ifndef ZIGBEE_MODE_ZCZR
#error "Select Tools -> Zigbee mode -> Coordinator/Router"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>

/* --- КОНФИГУРАЦИЯ --- */
#define MODEL_ID "C6_WATER_V1"
#define MANUFACTURER_NAME "MuseLab"
#define RGB_LED_PIN 8
#define BOOT_BUTTON_PIN 9 

Preferences prefs;
uint64_t val_cold = 0;
uint64_t val_hot = 0;

/* --- УТИЛИТЫ --- */
void setLed(uint8_t r, uint8_t g, uint8_t b) {
    neopixelWrite(RGB_LED_PIN, r, g, b);
}

void fill_uint48(uint8_t *dest, uint64_t value) {
    for (int i = 0; i < 6; i++) dest[i] = (value >> (i * 8)) & 0xFF;
}

/* --- КЛАСС УСТРОЙСТВА --- */
class ZigbeeWaterMeter : public ZigbeeEP {
public:
    ZigbeeWaterMeter(uint8_t endpoint) : ZigbeeEP(endpoint) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
    }

    void begin() {
        _cluster_list = esp_zb_zcl_cluster_list_create();

        // 1. Basic Cluster
        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x01 };
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        // 2. Identify Cluster
        esp_zb_identify_cluster_cfg_t id_cfg = { .identify_time = 0 };
        esp_zb_cluster_list_add_identify_cluster(_cluster_list, esp_zb_identify_cluster_create(&id_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        // 3. Metering Cluster
        esp_zb_attribute_list_t *meter_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);
        
        uint8_t def_uint48[6] = {0,0,0,0,0,0};
        uint8_t uom = 0x07; // Water Cubic Meters (Type: 8-bit enum 0x30)
        uint8_t fmt = 0x4B; // Formatting (Type: 8-bit bitmap/uint 0x18 или 0x20)
        uint8_t type = 0x02; // Water Meter (Type: 8-bit enum 0x30)

        // ИСПРАВЛЕНО: Добавлен второй аргумент (Cluster ID) во все вызовы
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, 0x25, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_uint48);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, 0x30, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &uom);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, 0x18, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &fmt);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, 0x30, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &type);

        esp_zb_cluster_list_add_metering_cluster(_cluster_list, meter_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        
        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    void reportValue(uint64_t value) {
        uint8_t zb_value[6];
        fill_uint48(zb_value, value);

        esp_zb_lock_acquire(portMAX_DELAY);
        // Обновляем локальное значение
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 
                                     ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, zb_value, false);
        
        // Формируем репорт
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
        
        Serial.printf("EP %d: Reported %llu\n", _endpoint, value);
    }
};

ZigbeeWaterMeter coldMeter(1);
ZigbeeWaterMeter hotMeter(2);

void checkButton() {
    static uint32_t btnTimer = 0;
    if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        if (btnTimer == 0) btnTimer = millis();
        if (millis() - btnTimer > 3000) { 
            Serial.println("FACTORY RESET...");
            setLed(50, 0, 0); 
            delay(1000);
            Zigbee.factoryReset();
            ESP.restart();
        }
    } else {
        btnTimer = 0;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    
    prefs.begin("water", false);
    val_cold = prefs.getULong64("c", 1000);
    val_hot = prefs.getULong64("h", 500);

    coldMeter.begin();
    hotMeter.begin();
    Zigbee.addEndpoint(&coldMeter);
    Zigbee.addEndpoint(&hotMeter);
    
    coldMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    hotMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    if (!Zigbee.begin(ZIGBEE_ROUTER)) {
        Serial.println("Zigbee Failed!");
        ESP.restart();
    }
    esp_zb_set_tx_power(20);
}

void loop() {
    static uint32_t last_report = 0;
    static uint32_t last_blink = 0;

    checkButton();

    if (Zigbee.connected()) {
        setLed(0, 1, 0); // Тускло-зеленый (в сети)

        if (millis() - last_report > 10000) { 
            last_report = millis();
            
            setLed(10, 10, 10); // Белая вспышка
            
            val_cold += 10;
            val_hot += 5;
            prefs.putULong64("c", val_cold);
            prefs.putULong64("h", val_hot);

            coldMeter.reportValue(val_cold);
            delay(500); 
            hotMeter.reportValue(val_hot);
        }
    } else {
        // Мигающий синий (поиск сети)
        if (millis() - last_blink > 500) {
            last_blink = millis();
            static bool toggle = false;
            toggle = !toggle;
            setLed(0, 0, toggle ? 15 : 0);
        }
    }
}
