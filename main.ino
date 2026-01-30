#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> End Device"
#endif

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include "esp_pm.h"
#include <Preferences.h>

#define MODEL_ID "C6_WATER_V2"
#define MANUFACTURER_NAME "MuseLab"

#define RGB_LED_PIN 8
#define BOOT_BUTTON_PIN 9 
#define PULSE_COLD_PIN 10
#define PULSE_HOT_PIN 11

#define SIMULATION_MODE 

Preferences prefs;
volatile uint64_t val_cold = 0;
volatile uint64_t val_hot = 0;
volatile bool needs_report = false;

/* --- КЛАСС УСТРОЙСТВА --- */
class ZigbeeWaterMeter : public ZigbeeEP {
private:
    bool _with_battery;

public:
    ZigbeeWaterMeter(uint8_t endpoint, bool with_battery = false) : ZigbeeEP(endpoint) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
        _with_battery = with_battery;
    }

    void begin() {
        _cluster_list = esp_zb_zcl_cluster_list_create();

        // 1. Basic Cluster (Battery Power)
        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x03 };
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);



        // 2. Power Config Cluster (Если нужен на этом эндпоинте)
        if (_with_battery) {
            uint8_t battery_perc = 200; // 100% (Zigbee формат: 0.5% units)
            esp_zb_attribute_list_t *power_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
            
            // Используем 0x0021 напрямую для BatteryPercentageRemaining
            esp_zb_cluster_add_attr(
                power_attr_list, 
                ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 
                0x0021, // ID атрибута BatteryPercentageRemaining
                0x20,   // Тип данных: Unsigned 8-bit integer
                ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, 
                &battery_perc
            );
            
            esp_zb_cluster_list_add_power_config_cluster(_cluster_list, power_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        }

        // 3. Metering Cluster
        esp_zb_attribute_list_t *meter_attr_list = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);
        uint8_t def_uint48[6] = {0,0,0,0,0,0};
        uint8_t uom = 0x07; uint8_t fmt = 0x4B; uint8_t type = 0x02;
        uint32_t multiplier = 1;      // Множитель 1
        uint32_t divisor = 1000;     // Делитель 1000 (так как шлем литры, а хотим м3)
        
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID, 0x25, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_uint48);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID, 0x30, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &uom);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID, 0x18, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &fmt);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID, 0x30, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &type);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0301, 0x22, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &multiplier);
        esp_zb_cluster_add_attr(meter_attr_list, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0302, 0x22, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &divisor);

        esp_zb_cluster_list_add_metering_cluster(_cluster_list, meter_attr_list, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        
        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    // Репорт батарейки
    void reportBattery(uint8_t percentage) {
        if (!_with_battery) return;
        uint8_t zb_val = percentage * 2; // 50% -> 100

        esp_zb_lock_acquire(portMAX_DELAY);
        // Снова используем 0x0021 вместо длинной константы
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

    void reportValue(uint64_t value) {
        uint8_t zb_value[6];
        for (int i = 0; i < 6; i++) zb_value[i] = (value >> (i * 8)) & 0xFF;
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
};

// Первый эндпоинт теперь с поддержкой батарейки
ZigbeeWaterMeter coldMeter(1, true); 
ZigbeeWaterMeter hotMeter(2, false);

void setup() {
    Serial.begin(115200);
    esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true };
    esp_pm_configure(&pm_config);

    prefs.begin("water", false);
    val_cold = prefs.getULong64("c", 1000);
    val_hot = prefs.getULong64("h", 500);

    coldMeter.begin(); hotMeter.begin();
    Zigbee.addEndpoint(&coldMeter); Zigbee.addEndpoint(&hotMeter);
    coldMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);
    hotMeter.setManufacturerAndModel(MANUFACTURER_NAME, MODEL_ID);

    Zigbee.begin(ZIGBEE_END_DEVICE);
}

void loop() {
    static uint32_t last_report = 0;
    static uint32_t last_battery_report = 0;
    static uint8_t fake_battery = 100;

    if (Zigbee.connected()) {
        #ifdef SIMULATION_MODE
        static uint32_t last_sim = 0;
        if (millis() - last_sim > 20000) {
            last_sim = millis();
            val_cold += 1; val_hot += 1;
            needs_report = true;
        }
        #endif

        // Репорт воды (раз в 10 сек при изменениях)
        if (needs_report && (millis() - last_report > 10000)) {
            last_report = millis();
            needs_report = false;
            prefs.putULong64("c", val_cold);
            prefs.putULong64("h", val_hot);
            coldMeter.reportValue(val_cold);
            delay(500);
            hotMeter.reportValue(val_hot);
        }

        // Репорт батарейки (раз в 60 секунд для теста, в реальности - раз в час)
        if (millis() - last_battery_report > 60000) {
            last_battery_report = millis();
            coldMeter.reportBattery(fake_battery);
            
            // Имитация разряда: уменьшаем на 1% при каждом репорте
            if (fake_battery > 10) fake_battery--; 
            else fake_battery = 100;
        }
    }
    delay(10);
}
