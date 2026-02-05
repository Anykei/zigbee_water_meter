#ifndef ZIGBEE_WATER_METER_H
#define ZIGBEE_WATER_METER_H

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>
#include <functional>

// Тип коллбэка для серийника
typedef std::function<void(uint32_t)> SerialChangeCallback;

class ZigbeeWaterMeter : public ZigbeeEP {
private:
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    SerialChangeCallback _onSerialChange = nullptr; 
    Preferences* _prefs = nullptr;

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
    void setPreferences(Preferences* p) { _prefs = p; }
    void onSerialChange(SerialChangeCallback cb) {
        _onSerialChange = cb;
    }
    // Потокобезопасные методы доступа
    void set_val(uint64_t v) { 
        if (v == this->value) return;
        portENTER_CRITICAL(&_mux); 
        this->value = v; 
        this->needs_immediate_report = true;
        portEXIT_CRITICAL(&_mux); 
    }

    uint64_t get_val() { 
        uint64_t v; portENTER_CRITICAL(&_mux); 
        v = this->value; 
        portEXIT_CRITICAL(&_mux); 
        return v; 
    }
    
    void set_offset(int32_t v) { this->offset_value = v; }
    void set_serial(uint32_t v) { 
        this->serial_number = v; 
        if (_onSerialChange) {
            _onSerialChange(v);
        }
    }

    uint32_t get_serial() { return this->serial_number; }
    void set_battery(uint8_t v) { this->battery_level = v; }

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
                auto value = *(int32_t *)message->attribute.data.value;
                this->set_offset(value);
                _prefs->putInt(_endpoint == 1 ? "oc" : "oh", offset_value);
                this->needs_immediate_report = true;
                Serial.printf("EP %d Offset -> %ld\n", _endpoint, offset_value);
            }
            else if (message->attribute.id == 0x0201) {
                auto serial = *(uint32_t *)message->attribute.data.value;
                this->set_serial(serial);
                _prefs->putUInt(_endpoint == 1 ? "sc" : "sh", serial_number);
                this->needs_immediate_report = true;
                Serial.printf("EP %d Serial -> %lu\n", _endpoint, serial_number);
            }
        }
    }
};

#endif