#ifndef ZIGBEE_WATER_METER_H
#define ZIGBEE_WATER_METER_H

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>
#include <functional>
#include "sources/water_source.h"

typedef std::function<void()> SettingsChangedCallback;

// Represents a Zigbee endpoint for a water meter.
//
// This class handles the interaction between the Zigbee stack (Clusters, Attributes)
// and the underlying WaterSource logic. It manages reporting values, handling
// configuration writes from the coordinator, and battery status.
class ZigbeeWaterMeter : public ZigbeeEP {
public:
    ZigbeeWaterMeter(uint8_t endpoint, bool with_battery = false) : 
        ZigbeeEP(endpoint), _with_battery(with_battery) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
    }

    void onSettingsChanged(SettingsChangedCallback cb) { _onSettingsChanged = cb; }

    void setSource(Source::WaterSource* s) { _source = s; }

    // Proxy methods interacting directly with the Source.
    void set_val(uint64_t v) { if (_source) _source->setLiters(v); }
    uint64_t get_val() { return _source ? _source->getLiters() : 0; }
    bool battery_supported() const { return _with_battery; }
    
    void set_offset(int32_t v) { if (_source) _source->setOffset(v); }
    void set_serial(uint32_t v) { if (_source) _source->setSerialNumber(v); }
    void set_battery(uint8_t v) { _battery_level = v; }

    uint32_t get_serial() { 
        return _source ? _source->getSerialNumber() : 0; 
    }

    int32_t get_offset() { 
        return _source ? _source->getOffset() : 0; 
    }

    // Checks if a report is required based on value changes or immediate flags.
    bool shouldReport() {
        if (!_source) return false;
        // If a write command for SN/Offset was received or data in source changed
        return needs_immediate_report || (_source->getTotalLiters() != _lastReportedValue);
    }

    void begin() {
        _cluster_list = esp_zb_zcl_cluster_list_create();

        // 1. Basic Cluster
        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x03 };
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        // 2. Power Config Cluster
        if (_with_battery) {
            uint8_t battery_perc = 200; 
            esp_zb_attribute_list_t *p_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
            esp_zb_cluster_add_attr(p_attr, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021, 0x20, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &battery_perc);
            esp_zb_cluster_list_add_power_config_cluster(_cluster_list, p_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        }

        // 3. Metering Cluster
        esp_zb_attribute_list_t *m_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);
        uint8_t def_u48[6] = {0};
        uint8_t uom = 0x07; uint8_t fmt = 0x4B; uint8_t type = 0x02;
        int32_t def_off = 0; uint32_t def_sn = 0;

        // Main Value (0x0000)
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0000, 0x25, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);
        
        // Custom Attribute: Hourly Consumption (0x0400) - liters
        uint32_t def_hourly = 0;
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0400, 0x23, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &def_hourly);

        // Settings (0x0100, 0x0102)
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0100, 0x25 /* u48 */, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0102, 0x25 /* u48 */, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);

        // Metadata (multiplier, divisor, etc.)
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0300, 0x30, 0x01, &uom);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0303, 0x18, 0x01, &fmt);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0306, 0x30, 0x01, &type);

        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0301, 0x21 /* u16 */, 0x01, &_multiplier);  // 0x21 вместо 0x22
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0302, 0x21 /* u16 */, 0x01, &_divisor);

        esp_zb_cluster_list_add_metering_cluster(_cluster_list, m_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    // Reports the current total volume to the Zigbee coordinator.
    void reportValue() {
        if (!_source) return;
        uint64_t total = _source->getTotalLiters();
        uint8_t zb_u48[6];
        for (int i = 0; i < 6; i++) zb_u48[i] = (total >> (i * 8)) & 0xFF;

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0000, zb_u48, false);
        sendReportCmd(0x0000); 
        esp_zb_lock_release();

        _lastReportedValue = _source->getTotalLiters();
    }

    // Reports the consumption for the last completed hour.
    void reportHourly() {
        if (!_source) return;
        uint32_t hourly = (uint32_t)_source->getLastHourConsumption();

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0400, &hourly, false);
        sendReportCmd(0x0400); 
        esp_zb_lock_release();
        
        Serial.printf("EP %d: Reported LAST HOUR consumption: %u\n", _endpoint, hourly);
    }

    // Reports the battery percentage.
    void reportBattery() {
        if (!_with_battery) return;
        uint8_t zb_val = _battery_level * 2;

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

        // esp_zb_lock_acquire(portMAX_DELAY);
        // esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0021, &zb_val, false);
        // esp_zb_lock_release();
        // sendReportCmd(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021);
    }

    // Reports configuration attributes (Offset and Serial Number).
    void reportConfig() {
        if (!_source) return;
        
        // Pack 32-bit numbers into 48-bit buffers (6 bytes)
        auto packU48 = [](uint32_t val, uint8_t* buf) {
            memset(buf, 0, 6);
            memcpy(buf, &val, 4);
        };

        uint8_t buf_off[6]; packU48((uint32_t)_source->getOffset(), buf_off);
        uint8_t buf_sn[6];  packU48(_source->getSerialNumber(), buf_sn);

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0100, buf_off, false);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0102, buf_sn, false);
        
        sendReportCmd(0x0100); 
        delay(100);
        sendReportCmd(0x0102); 
        esp_zb_lock_release();
    }

    // Handles incoming write requests for attributes.
    void handleAttributeWrite(const esp_zb_zcl_set_attr_value_message_t *message) {
        if (!_source) return;
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_METERING) {
            // Helper to safely unpack a 32-bit value from a 48-bit (6-byte) buffer
            auto unpackU32 = [](const esp_zb_zcl_attribute_t* attr) -> uint32_t {
                if (attr->data.type == ESP_ZB_ZCL_ATTR_TYPE_U48 && attr->data.size >= 4) {
                    uint32_t val = 0;
                    memcpy(&val, attr->data.value, sizeof(val));
                    return val;
                }
                return 0; // Or handle error
            };

            uint16_t id = message->attribute.id;
            uint32_t val = unpackU32(&message->attribute);
            bool changed = false;

            if (id == 0x100) { 
                _source->setOffset((int32_t)val);
                changed = true;
            }
            else if (id == 0x102) { 
                _source->setSerialNumber(val);
                changed = true;
            }

            if (changed && _onSettingsChanged) _onSettingsChanged();
        }
    }

private:
    void sendReportCmd(uint16_t attrId, uint16_t clusterId = ESP_ZB_ZCL_CLUSTER_ID_METERING) {
        esp_zb_zcl_report_attr_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        cmd.clusterID = clusterId;
        cmd.attributeID = attrId;
        cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        cmd.zcl_basic_cmd.src_endpoint = _endpoint;
        cmd.zcl_basic_cmd.dst_endpoint = 1; 
        cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
        esp_zb_zcl_report_attr_cmd_req(&cmd);
    }

    SettingsChangedCallback _onSettingsChanged = nullptr;
    Source::WaterSource* _source = nullptr;

    bool _with_battery;

    uint8_t _battery_level = 100;
    
    uint16_t _multiplier = 1;
    uint16_t _divisor = 1000;

    uint64_t _lastReportedValue = 0xFFFFFFFFFFFFFFFF; // Initialize to "unknown" to ensure first report
    bool needs_immediate_report = false;
};


#endif