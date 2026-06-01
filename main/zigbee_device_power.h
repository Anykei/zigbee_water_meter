/*
 * Класс для мониторинга питания самого ESP32 (отдельно от счетчиков).
 * Реализует Zigbee Endpoint с кластером Power Config.
 */

#ifndef ZIGBEE_DEVICE_POWER_H
#define ZIGBEE_DEVICE_POWER_H

#include "Zigbee.h"
#include "esp_zigbee_core.h"
#include <Arduino.h>

class ZigbeeDevicePower : public ZigbeeEP {
public:
    ZigbeeDevicePower(
        uint8_t endpoint,
        int adc_pin,
        float v_min,
        float v_max,
        float adc_to_input_scale,
        int32_t input_offset_mv,
        int adc_enable_pin,
        bool adc_enable_active_low,
        uint16_t adc_power_settle_ms,
        uint8_t adc_samples,
        uint16_t adc_sample_delay_ms,
        uint16_t curve_empty_mv,
        uint16_t curve_low_mv,
        uint16_t curve_mid_mv,
        uint16_t curve_high_mv,
        uint16_t curve_full_mv
    ) :
        ZigbeeEP(endpoint),
        _adc_pin(adc_pin),
        _v_min(v_min),
        _v_max(v_max),
        _adc_to_input_scale(adc_to_input_scale),
        _input_offset_mv(input_offset_mv),
        _adc_enable_pin(adc_enable_pin),
        _adc_enable_active_low(adc_enable_active_low),
        _adc_power_settle_ms(adc_power_settle_ms),
        _adc_samples(adc_samples > 0 ? adc_samples : 1),
        _adc_sample_delay_ms(adc_sample_delay_ms),
        _curve_empty_mv(curve_empty_mv),
        _curve_low_mv(curve_low_mv),
        _curve_mid_mv(curve_mid_mv),
        _curve_high_mv(curve_high_mv),
        _curve_full_mv(curve_full_mv) {
        _device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID; 
    }

    void begin() {
        if (_adc_pin >= 0) {
            pinMode(_adc_pin, INPUT);
        }
        if (_adc_enable_pin >= 0) {
            pinMode(_adc_enable_pin, OUTPUT);
            setAdcPower(false);
        }

        _cluster_list = esp_zb_zcl_cluster_list_create();

        // 1. Basic Cluster
        // power_source = 0x03 (Battery)
        esp_zb_basic_cluster_cfg_t basic_cfg = { .zcl_version = 3, .power_source = 0x03 }; 
        esp_zb_cluster_list_add_basic_cluster(_cluster_list, esp_zb_basic_cluster_create(&basic_cfg), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        // 2. Power Config Cluster
        esp_zb_power_config_cluster_cfg_t power_cfg;
        memset(&power_cfg, 0, sizeof(power_cfg));
        
        // Добавляем атрибуты: Напряжение (0x0020) и Процент (0x0021)
        esp_zb_attribute_list_t *p_attr = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG);
        
        uint8_t battery_perc = 0;
        uint8_t battery_voltage = 0; // в единицах 100mV

        esp_zb_cluster_add_attr(p_attr, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0020, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &battery_voltage);
        esp_zb_cluster_add_attr(p_attr, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, 0x0021, ESP_ZB_ZCL_ATTR_TYPE_U8, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &battery_perc);
        
        esp_zb_cluster_list_add_power_config_cluster(_cluster_list, p_attr, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        _ep_config = { .endpoint = _endpoint, .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID, .app_device_id = _device_id, .app_device_version = 0 };
    }

    uint8_t reportStatus() {
        // Raw ADC voltage is converted to the real measured input voltage.
        uint32_t raw_mv = readRawMilliVolts();
        int32_t calibrated_mv = (int32_t)((raw_mv * _adc_to_input_scale) + _input_offset_mv + 0.5f);
        if (calibrated_mv < 0) calibrated_mv = 0;
        
        uint8_t percentage = voltageToPercent((uint32_t)calibrated_mv);
        uint8_t zb_percentage = (uint8_t)(percentage * 2);

        // 2. Расчет напряжения для Zigbee (в единицах 100mV, т.е. 3.3В = 33)
        uint8_t zb_voltage = (uint8_t)(calibrated_mv / 100);

        esp_zb_lock_acquire(portMAX_DELAY);
        
        // Обновляем и репортим процент
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0021, &zb_percentage, false);
        sendReportCmd(0x0021);

        // Обновляем и репортим напряжение
        esp_zb_zcl_set_attribute_val(_endpoint, ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, 0x0020, &zb_voltage, false);
        // sendReportCmd(0x0020); // Можно не отправлять принудительно, если достаточно процента

        esp_zb_lock_release();

        Serial.printf(
            "EP %d (System Power): raw=%lu mV, calibrated=%ld mV (%d%%)\n",
            _endpoint,
            (unsigned long)raw_mv,
            (long)calibrated_mv,
            (int)percentage
        );

        _last_raw_mv = raw_mv;
        _last_calibrated_mv = (uint32_t)calibrated_mv;
        _last_percent = percentage;
        return percentage;
    }

    uint32_t lastRawMilliVolts() const { return _last_raw_mv; }
    uint32_t lastCalibratedMilliVolts() const { return _last_calibrated_mv; }
    uint8_t lastPercent() const { return _last_percent; }

private:
    void setAdcPower(bool on) {
        if (_adc_enable_pin < 0) return;
        const bool level = _adc_enable_active_low ? !on : on;
        digitalWrite(_adc_enable_pin, level ? HIGH : LOW);
    }

    uint32_t readRawMilliVolts() {
        if (_adc_pin < 0) return 0;

        setAdcPower(true);
        if (_adc_power_settle_ms > 0) delay(_adc_power_settle_ms);

        uint32_t total_mv = 0;
        for (uint8_t i = 0; i < _adc_samples; ++i) {
            total_mv += analogReadMilliVolts(_adc_pin);
            if (i + 1 < _adc_samples && _adc_sample_delay_ms > 0) {
                delay(_adc_sample_delay_ms);
            }
        }

        setAdcPower(false);
        return total_mv / _adc_samples;
    }

    static uint8_t interpolatePercent(uint32_t mv, uint16_t from_mv, uint8_t from_pct, uint16_t to_mv, uint8_t to_pct) {
        if (to_mv <= from_mv) return to_pct;
        const uint32_t span_mv = to_mv - from_mv;
        const uint32_t offset_mv = mv - from_mv;
        const uint32_t span_pct = to_pct - from_pct;
        return from_pct + (uint8_t)((offset_mv * span_pct) / span_mv);
    }

    uint8_t voltageToPercent(uint32_t mv) const {
        if (mv <= _curve_empty_mv) return 0;
        if (mv >= _curve_full_mv) return 100;
        if (mv < _curve_low_mv)  return interpolatePercent(mv, _curve_empty_mv, 0,  _curve_low_mv, 15);
        if (mv < _curve_mid_mv)  return interpolatePercent(mv, _curve_low_mv,   15, _curve_mid_mv, 45);
        if (mv < _curve_high_mv) return interpolatePercent(mv, _curve_mid_mv,   45, _curve_high_mv, 75);
        return interpolatePercent(mv, _curve_high_mv, 75, _curve_full_mv, 100);
    }

    void sendReportCmd(uint16_t attrId) {
        esp_zb_zcl_report_attr_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        
        cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
        cmd.clusterID = ESP_ZB_ZCL_CLUSTER_ID_POWER_CONFIG;
        cmd.attributeID = attrId;
        cmd.direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI;
        cmd.zcl_basic_cmd.src_endpoint = _endpoint;
        cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000; // Адрес координатора
        cmd.zcl_basic_cmd.dst_endpoint = 1;               // Стандартный эндпоинт координатора
        
        esp_zb_zcl_report_attr_cmd_req(&cmd);
    }

    int _adc_pin;
    float _v_min;
    float _v_max;
    float _adc_to_input_scale;
    int32_t _input_offset_mv;
    int _adc_enable_pin;
    bool _adc_enable_active_low;
    uint16_t _adc_power_settle_ms;
    uint8_t _adc_samples;
    uint16_t _adc_sample_delay_ms;
    uint16_t _curve_empty_mv;
    uint16_t _curve_low_mv;
    uint16_t _curve_mid_mv;
    uint16_t _curve_high_mv;
    uint16_t _curve_full_mv;
    uint32_t _last_raw_mv = 0;
    uint32_t _last_calibrated_mv = 0;
    uint8_t _last_percent = 0;
};

#endif
