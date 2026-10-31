const exposes = require('zigbee-herdsman-converters/lib/exposes');
const ea = exposes.access;

const ATTR_HOURLY_CONSUMPTION = 0xE000;
const ATTR_REFRESH_REQUEST = 0xE001;
const ATTR_POLL_INTERVAL_MINUTES = 0xE002;
const ATTR_METER_BATTERY_VOLTAGE = 0xE003;
const WRITE_OPTIONS = {disableResponse: true};

const parseValue = (val) => {
    if (Buffer.isBuffer(val)) return Number(val.readUIntLE(0, val.length));
    if (val && typeof val === 'object' && Object.prototype.hasOwnProperty.call(val, 'value')) {
        return parseValue(val.value);
    }
    return Number(val);
};

const parseSignedU32 = (val) => {
    let unsigned = parseValue(val) % 0x100000000;
    if (unsigned < 0) unsigned += 0x100000000;
    return unsigned >= 0x80000000 ? unsigned - 0x100000000 : unsigned;
};

const encodeSignedU32 = (val) => {
    const signed = Math.max(-0x80000000, Math.min(0x7FFFFFFF, Math.trunc(val)));
    return signed < 0 ? 0x100000000 + signed : signed;
};

const getAttr = (data, attrId) => {
    if (Object.prototype.hasOwnProperty.call(data, attrId)) return data[attrId];

    const key = String(attrId);
    if (Object.prototype.hasOwnProperty.call(data, key)) return data[key];

    return undefined;
};

const definition = {
    zigbeeModel: ['C6_WATER_METER'],
    model: 'C6_WATER_METER',
    vendor: 'MuseLab',
    description: 'Двухканальный счетчик воды ESP32-C6',
    icon: 'mdi:water-pump',
    fromZigbee: [
        {
            cluster: 'seMetering',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                const ep = msg.endpoint.ID;
                const data = msg.data;

                if (Object.prototype.hasOwnProperty.call(data, 'currentSummDelivered')) {
                    result[`water_total_${ep}`] = parseValue(data['currentSummDelivered']) / 1000;
                }
                if (Object.prototype.hasOwnProperty.call(data, 'instantaneousDemand')) {
                    result[`flow_rate_${ep}`] = parseValue(data['instantaneousDemand']) / 1000;
                }

                const hourlyConsumption = getAttr(data, ATTR_HOURLY_CONSUMPTION);
                if (hourlyConsumption !== undefined) {
                    result[`hourly_consumption_${ep}`] = parseValue(hourlyConsumption) / 1000;
                }

                const pollInterval = getAttr(data, ATTR_POLL_INTERVAL_MINUTES);
                if (pollInterval !== undefined) {
                    result[`poll_interval_${ep}`] = parseValue(pollInterval);
                }

                const meterBatteryVoltage = getAttr(data, ATTR_METER_BATTERY_VOLTAGE);
                if (meterBatteryVoltage !== undefined) {
                    result[`meter_battery_voltage_${ep}`] = parseValue(meterBatteryVoltage) / 1000;
                }

                // The firmware reuses Tier attributes for writable settings.
                if (Object.prototype.hasOwnProperty.call(data, 'currentTier1SummDelivered')) {
                    result[`offset_${ep}`] = parseSignedU32(data['currentTier1SummDelivered']) / 1000;
                }
                if (Object.prototype.hasOwnProperty.call(data, 'currentTier2SummDelivered')) {
                    result[`serial_${ep}`] = parseValue(data['currentTier2SummDelivered']);
                }

                return result;
            },
        },
        {
            cluster: 'genPowerCfg',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (Object.prototype.hasOwnProperty.call(msg.data, 'batteryPercentageRemaining')) {
                    return {battery: Math.round(msg.data['batteryPercentageRemaining'] / 2)};
                }

                return undefined;
            },
        },
    ],
    toZigbee: [
        {
            key: ['offset', 'serial', 'refresh', 'poll_interval'],
            convertSet: async (entity, key, value, meta) => {
                if (key === 'offset') {
                    const liters = Math.round(value * 1000);
                    await entity.write('seMetering', {0x0100: {value: encodeSignedU32(liters), type: 0x25}}, WRITE_OPTIONS);
                    return {state: {offset: value}};
                }

                if (key === 'serial') {
                    await entity.write('seMetering', {0x0102: {value: value, type: 0x25}}, WRITE_OPTIONS);
                    return {state: {serial: value}};
                }

                if (key === 'refresh') {
                    await entity.write('seMetering', {[ATTR_REFRESH_REQUEST]: {value: 1, type: 0x20}}, WRITE_OPTIONS);
                    return {state: {refresh: 'OFF'}};
                }

                if (key === 'poll_interval') {
                    const minutes = Math.max(1, Math.min(1440, Math.round(Number(value))));
                    await entity.write('seMetering', {[ATTR_POLL_INTERVAL_MINUTES]: {value: minutes, type: 0x21}}, WRITE_OPTIONS);
                    return {state: {poll_interval: minutes}};
                }

                return undefined;
            },
        },
    ],
    exposes: [
        {
            type: 'numeric',
            name: 'water_total',
            label: 'Общий расход',
            endpoint: '1',
            property: 'water_total_1',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'total_increasing',
            icon: 'mdi:counter',
        },
        {
            type: 'numeric',
            name: 'hourly_consumption',
            label: 'Расход за последний час',
            endpoint: '1',
            property: 'hourly_consumption_1',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'measurement',
            icon: 'mdi:water-plus',
        },
        {
            type: 'numeric',
            name: 'flow_rate',
            label: 'Текущий расход',
            endpoint: '1',
            property: 'flow_rate_1',
            access: ea.STATE,
            unit: 'm³/h',
            device_class: 'volume_flow_rate',
            state_class: 'measurement',
            icon: 'mdi:gauge',
        },
        {
            type: 'numeric',
            name: 'offset',
            label: 'Калибровочный сдвиг',
            endpoint: '1',
            property: 'offset_1',
            access: ea.STATE_SET,
            unit: 'm³',
            category: 'config',
            icon: 'mdi:wrench',
        },
        {
            type: 'numeric',
            name: 'serial',
            label: 'Серийный номер',
            endpoint: '1',
            property: 'serial_1',
            access: ea.STATE_SET,
            category: 'config',
            icon: 'mdi:identifier',
        },
        {
            type: 'binary',
            name: 'refresh',
            label: 'Обновить значения',
            endpoint: '1',
            property: 'refresh_1',
            access: ea.SET,
            value_on: 'ON',
            value_off: 'OFF',
            category: 'config',
            icon: 'mdi:refresh',
        },
        {
            type: 'numeric',
            name: 'poll_interval',
            label: 'Интервал опроса',
            endpoint: '1',
            property: 'poll_interval_1',
            access: ea.STATE_SET,
            unit: 'мин',
            value_min: 1,
            value_max: 1440,
            value_step: 1,
            category: 'config',
            icon: 'mdi:timer-cog',
        },
        {
            type: 'numeric',
            name: 'meter_battery_voltage',
            label: 'Напряжение батареи счетчика',
            endpoint: '1',
            property: 'meter_battery_voltage_1',
            access: ea.STATE,
            unit: 'V',
            device_class: 'voltage',
            state_class: 'measurement',
            category: 'diagnostic',
            icon: 'mdi:battery',
        },
        {
            type: 'numeric',
            name: 'water_total',
            label: 'Общий расход',
            endpoint: '2',
            property: 'water_total_2',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'total_increasing',
            icon: 'mdi:counter',
        },
        {
            type: 'numeric',
            name: 'hourly_consumption',
            label: 'Расход за последний час',
            endpoint: '2',
            property: 'hourly_consumption_2',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'measurement',
            icon: 'mdi:water-plus',
        },
        {
            type: 'numeric',
            name: 'flow_rate',
            label: 'Текущий расход',
            endpoint: '2',
            property: 'flow_rate_2',
            access: ea.STATE,
            unit: 'm³/h',
            device_class: 'volume_flow_rate',
            state_class: 'measurement',
            icon: 'mdi:gauge',
        },
        {
            type: 'numeric',
            name: 'offset',
            label: 'Калибровочный сдвиг',
            endpoint: '2',
            property: 'offset_2',
            access: ea.STATE_SET,
            unit: 'm³',
            category: 'config',
            icon: 'mdi:wrench',
        },
        {
            type: 'numeric',
            name: 'serial',
            label: 'Серийный номер',
            endpoint: '2',
            property: 'serial_2',
            access: ea.STATE_SET,
            category: 'config',
            icon: 'mdi:identifier',
        },
        {
            type: 'binary',
            name: 'refresh',
            label: 'Обновить значения',
            endpoint: '2',
            property: 'refresh_2',
            access: ea.SET,
            value_on: 'ON',
            value_off: 'OFF',
            category: 'config',
            icon: 'mdi:refresh',
        },
        {
            type: 'numeric',
            name: 'poll_interval',
            label: 'Интервал опроса',
            endpoint: '2',
            property: 'poll_interval_2',
            access: ea.STATE_SET,
            unit: 'мин',
            value_min: 1,
            value_max: 1440,
            value_step: 1,
            category: 'config',
            icon: 'mdi:timer-cog',
        },
        {
            type: 'numeric',
            name: 'meter_battery_voltage',
            label: 'Напряжение батареи счетчика',
            endpoint: '2',
            property: 'meter_battery_voltage_2',
            access: ea.STATE,
            unit: 'V',
            device_class: 'voltage',
            state_class: 'measurement',
            category: 'diagnostic',
            icon: 'mdi:battery',
        },
        {
            type: 'numeric',
            name: 'battery',
            property: 'battery',
            access: ea.STATE,
            unit: '%',
            device_class: 'battery',
            state_class: 'measurement',
            category: 'diagnostic',
            icon: 'mdi:battery',
            label: 'Батарея контроллера',
        },
    ],
    meta: {multiEndpoint: true},
    endpoint: (device) => ({'1': 1, '2': 2}),
};

module.exports = definition;
