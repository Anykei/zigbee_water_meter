#ifndef ZIGBEE_WATER_METER_H
#define ZIGBEE_WATER_METER_H

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Preferences.h>
#include <functional>
#include <atomic>
#include "sources/water_source.h"

typedef std::function<void()> SettingsChangedCallback;

// Zigbee endpoint that maps a WaterSource onto Metering attributes and reports.
// The coordinator writes calibration settings back through the same endpoint.

// Tier attributes carry project-specific settings because Zigbee2MQTT already
// understands their U48 wire format for Metering clusters.
static constexpr uint16_t kAttrIdOffset = 0x0100;
static constexpr uint16_t kAttrIdSerialNumber = 0x0102;
static constexpr uint16_t kAttrFlowRate = 0x0400;
static constexpr uint16_t kAttrHourlyConsumption = 0xE000;
static constexpr uint16_t kAttrRefreshRequest = 0xE001;
static constexpr uint16_t kAttrPollIntervalMinutes = 0xE002;
static constexpr uint16_t kAttrMeterBatteryVoltage = 0xE003;

class ZigbeeWaterMeter : public ZigbeeEP {
public:
    ZigbeeWaterMeter(uint8_t endpoint, bool with_battery = false) : 
        ZigbeeEP(endpoint), _with_battery(with_battery) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID;
    }

    bool isConfigDirty() const { return _config_dirty; }
    void clearConfigDirty() { _config_dirty = false; }
    bool needsConfigReport() const { return _needs_config_report; }

    void setSource(Source::WaterSource* s) { _source = s; }

    void set_val(uint64_t v) { if (_source) _source->setLiters(v); }
    uint64_t get_val() { return _source ? _source->getLiters() : 0; }
    bool battery_supported() const { return _with_battery; }
    bool readings_valid() const { return _source ? _source->hasValidReadings() : false; }
    
    void set_offset(int32_t v) { if (_source) _source->setOffset(v); }
    void set_serial(uint32_t v) { if (_source) _source->setSerialNumber(v); }
    void set_poll_interval_minutes(uint32_t v) { if (_source) _source->setPollIntervalMinutes(v); }
    void set_battery(uint8_t v) { _battery_level = v; }

    uint32_t get_serial() { 
        return _source ? _source->getSerialNumber() : 0; 
    }

    int32_t get_offset() { 
        return _source ? _source->getOffset() : 0; 
    }

    uint16_t get_poll_interval_minutes() {
        return _source ? _source->getPollIntervalMinutes() : Source::kMinPollIntervalMinutes;
    }

    bool shouldReport() {
        if (!_source) return false;
        if (!_source->hasValidReadings()) return false;

        return _needs_immediate_report ||
            _source->hasFreshReportReady() ||
            (_source->getTotalLiters() != _lastReportedValue) ||
            (flowRateMilliM3PerHour() != _lastReportedFlowRate) ||
            (_source->hasBatteryVoltage() && meterBatteryMilliVolts() != _lastReportedMeterBatteryMv);
    }

    void begin() {
        _cluster_list = esp_zb_zcl_cluster_list_create();

        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x03 };
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        if (_with_battery) {
            uint8_t battery_perc = 200; 
            esp_zb_attribute_list_t *p_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
            esp_zb_cluster_add_attr(p_attr, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021, 0x20, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &battery_perc);
            esp_zb_cluster_list_add_power_config_cluster(_cluster_list, p_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        }

        esp_zb_attribute_list_t *m_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);
        uint8_t def_u48[6] = {0};
        uint8_t uom = 0x07; uint8_t fmt = 0x4B; uint8_t type = 0x02;

        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0000, ESP_ZB_ZCL_ATTR_TYPE_U48, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);
        
        uint32_t def_flow = 0;
        uint32_t def_hourly = 0;
        uint8_t def_refresh = 0;
        uint16_t def_poll_interval = _source ? _source->getPollIntervalMinutes() : Source::kMinPollIntervalMinutes;
        uint16_t def_meter_battery_mv = 0;
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrFlowRate, ESP_ZB_ZCL_ATTR_TYPE_U32, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &def_flow);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrHourlyConsumption, ESP_ZB_ZCL_ATTR_TYPE_U32, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &def_hourly);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrRefreshRequest, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_WRITE_ONLY, &def_refresh);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrPollIntervalMinutes, ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &def_poll_interval);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrMeterBatteryVoltage, ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &def_meter_battery_mv);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrIdOffset, ESP_ZB_ZCL_ATTR_TYPE_U48, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, kAttrIdSerialNumber, ESP_ZB_ZCL_ATTR_TYPE_U48, ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, def_u48);

        // Standard Metering metadata tells consumers that values are cubic
        // meters with a divisor of 1000.
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0300, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &uom);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0303, ESP_ZB_ZCL_ATTR_TYPE_8BITMAP, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &fmt);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0306, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &type);

        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0301, ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &_multiplier);
        esp_zb_cluster_add_attr(m_attr, ESP_ZB_ZCL_CLUSTER_ID_METERING, 0x0302, ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &_divisor);

        esp_zb_cluster_list_add_metering_cluster(_cluster_list, m_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    void reportValue() {
        if (!_source) return;
        if (!_source->hasValidReadings()) return;

        uint64_t total = _source->getTotalLiters();
        uint32_t flow = flowRateMilliM3PerHour();
        uint16_t meterBatteryMv = meterBatteryMilliVolts();
        uint8_t zb_u48[6];
        for (int i = 0; i < 6; i++) zb_u48[i] = (total >> (i * 8)) & 0xFF;

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0000, zb_u48, false);
        sendReportCmd(0x0000);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrFlowRate, &flow, false);
        sendReportCmd(kAttrFlowRate);
        if (_source->hasBatteryVoltage()) {
            esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrMeterBatteryVoltage, &meterBatteryMv, false);
            sendReportCmd(kAttrMeterBatteryVoltage);
        }
        esp_zb_lock_release();

        _lastReportedValue = total;
        _lastReportedFlowRate = flow;
        if (_source->hasBatteryVoltage()) _lastReportedMeterBatteryMv = meterBatteryMv;
        _needs_immediate_report = false;
        _source->clearFreshReportReady();
    }

    void reportHourly() {
        if (!_source) return;
        uint32_t hourly = (uint32_t)_source->getLastHourConsumption();

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrHourlyConsumption, &hourly, false);
        sendReportCmd(kAttrHourlyConsumption);
        esp_zb_lock_release();
        
        Serial.printf("EP %d: Reported LAST HOUR consumption: %lu\n", _endpoint, (unsigned long)hourly);
    }

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
        
        sendReportCmd(0x0021, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
        esp_zb_lock_release();
    }

    // Reports coordinator-writable settings after startup and Zigbee writes.
    void reportConfig() {
        if (!_source) return;
        
        // Custom settings use U48 attributes, but their payloads are 32-bit
        // application values padded to six bytes.
        auto packU48 = [](uint32_t val, uint8_t* buf) {
            memset(buf, 0, 6);
            memcpy(buf, &val, 4);
        };

        uint8_t buf_off[6]; packU48((uint32_t)_source->getOffset(), buf_off);
        uint8_t buf_sn[6];  packU48(_source->getSerialNumber(), buf_sn);
        uint16_t poll_interval = _source->getPollIntervalMinutes();

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrIdOffset, buf_off, false);
        sendReportCmd(kAttrIdOffset);
        esp_zb_lock_release();

        delay(100);

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrIdSerialNumber, buf_sn, false);
        sendReportCmd(kAttrIdSerialNumber);
        esp_zb_lock_release();

        delay(100);

        esp_zb_lock_acquire(portMAX_DELAY);
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_METERING, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, kAttrPollIntervalMinutes, &poll_interval, false);
        sendReportCmd(kAttrPollIntervalMinutes);
        esp_zb_lock_release();

        Serial.printf(
            "EP %d: Reported config -> Offset:%ld, Serial:%lu, Poll:%u min.\n",
            _endpoint,
            (long)_source->getOffset(),
            (unsigned long)_source->getSerialNumber(),
            (unsigned)poll_interval
        );

        _needs_immediate_report = false;
        _needs_config_report = false;
    }

    void handleAttributeWrite(const esp_zb_zcl_set_attr_value_message_t *message) {
        if (!_source) return;
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_METERING) {
            uint16_t id = message->attribute.id;
            if (id == kAttrRefreshRequest) {
                _source->requestFreshReport();
                Serial.printf("EP %d: Refresh requested from coordinator.\n", _endpoint);
                return;
            }

            if (id == kAttrPollIntervalMinutes) {
                if (message->attribute.data.type != ESP_ZB_ZCL_ATTR_TYPE_U16 || message->attribute.data.value == nullptr) return;

                uint16_t minutes = 0;
                size_t copySize = message->attribute.data.size < sizeof(minutes) ? message->attribute.data.size : sizeof(minutes);
                memcpy(&minutes, message->attribute.data.value, copySize);
                _source->setPollIntervalMinutes(minutes);
                _config_dirty = true;
                _needs_config_report = true;
                Serial.printf("EP %d: Poll interval set to %u min.\n", _endpoint, _source->getPollIntervalMinutes());
                return;
            }

            auto unpackU32 = [](const esp_zb_zcl_attribute_t* attr, uint32_t& out_val) -> bool {
                if (attr->data.type != ESP_ZB_ZCL_ATTR_TYPE_U48) return false;
                
                uint64_t temp_val = 0;
                memcpy(&temp_val, attr->data.value, attr->data.size > 6 ? 6 : attr->data.size);

                if (temp_val > UINT32_MAX) {
                    Serial.printf("Error: Received U48 value %llu exceeds U32 max for attribute 0x%04X\n", temp_val, attr->id);
                    return false;
                }
                out_val = (uint32_t)temp_val;
                return true;
            };

            uint32_t val;
            if (!unpackU32(&message->attribute, val)) return;

            bool changed = false;
            bool needsValueReport = false;

            if (id == kAttrIdOffset) { 
                _source->setOffset((int32_t)val);
                changed = true;
                needsValueReport = true;
            }
            else if (id == kAttrIdSerialNumber) { 
                if (_source->getSerialNumber() != val) {
                    _source->resetReadingsForNewMeter();
                } else {
                    _source->requestFreshReport();
                }
                _source->setSerialNumber(val);
                changed = true;
            }

            if (changed) {
                _config_dirty = true;
                _needs_config_report = true;
                _needs_immediate_report = needsValueReport;
            }
        }
    }

private:
    uint32_t flowRateMilliM3PerHour() const {
        if (!_source) return 0;

        float flowRateM3PerHour = _source->getFlowRateM3PerHour();
        if (flowRateM3PerHour <= 0.0f) return 0;

        const double scaled = (double)flowRateM3PerHour * 1000.0;
        if (scaled >= UINT32_MAX) return UINT32_MAX;
        return (uint32_t)(scaled + 0.5);
    }

    uint16_t meterBatteryMilliVolts() const {
        return _source ? _source->getBatteryMilliVolts() : 0;
    }

    void sendReportCmd(uint16_t attrId, uint16_t clusterId = ESP_ZB_ZCL_CLUSTER_ID_METERING) {
        esp_zb_zcl_report_attr_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        // Send directly to the coordinator because a sleepy end device may not
        // have a populated binding table after commissioning.
        cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        cmd.clusterID = clusterId;
        cmd.attributeID = attrId;
        cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        cmd.zcl_basic_cmd.src_endpoint = _endpoint;
        cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;
        cmd.zcl_basic_cmd.dst_endpoint = 1;
        esp_zb_zcl_report_attr_cmd_req(&cmd);
    }

    Source::WaterSource* _source = nullptr;

    bool _with_battery;

    uint8_t _battery_level = 100;
    
    uint16_t _multiplier = 1;
    uint16_t _divisor = 1000;

    uint64_t _lastReportedValue = 0xFFFFFFFFFFFFFFFF;
    uint32_t _lastReportedFlowRate = UINT32_MAX;
    uint16_t _lastReportedMeterBatteryMv = UINT16_MAX;
    bool _needs_immediate_report = false;
    std::atomic<bool> _needs_config_report{false};
    std::atomic<bool> _config_dirty{false};
};


#endif  // ZIGBEE_WATER_METER_H
