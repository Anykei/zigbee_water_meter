const exposes = require('zigbee-herdsman-converters/lib/exposes');
const ea = exposes.access;

const definition = {
    zigbeeModel: ['C6_WATER_METER'],
    model: 'C6_WATER_METER',
    vendor: 'MuseLab',
    description: 'Professional Dual Channel Water Meter ESP32-C6',
    fromZigbee: [
        {
            cluster: 'seMetering',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                const ep = msg.endpoint.ID;
                const data = msg.data;

                const parseValue = (val) => {
                    if (Buffer.isBuffer(val)) return Number(val.readUIntLE(0, val.length));
                    return Number(val);
                };

                // Стандартные атрибуты Metering
                if (data.hasOwnProperty('currentSummDelivered')) {
                    result[`water_total_${ep}`] = parseValue(data['currentSummDelivered']) / 1000;
                }
                if (data.hasOwnProperty('instantaneousDemand')) {
                    result[`hourly_consumption_${ep}`] = parseValue(data['instantaneousDemand']) / 1000;
                }
                // Оффсет (Tier 1) и Серийник (Tier 2)
                if (data.hasOwnProperty('currentTier1SummDelivered')) {
                    result[`offset_${ep}`] = parseValue(data['currentTier1SummDelivered']) / 1000;
                }
                if (data.hasOwnProperty('currentTier2SummDelivered')) {
                    result[`serial_${ep}`] = parseValue(data['currentTier2SummDelivered']);
                }
                return result;
            },
        },
        {
            cluster: 'genPowerCfg',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                if (msg.data.hasOwnProperty('batteryPercentageRemaining')) {
                    return { battery: Math.round(msg.data['batteryPercentageRemaining'] / 2) };
                }
            },
        }
    ],
    toZigbee: [
        {
            key: ['offset', 'serial'],
            convertSet: async (entity, key, value, meta) => {
                const epId = entity.ID; 
                if (key === 'offset') {
                    const liters = Math.round(value * 1000);
                    await entity.write('seMetering', {0x0100: {value: liters, type: 0x25}});
                    return {state: {[`offset_${epId}`]: value}};
                } 
                else if (key === 'serial') {
                    await entity.write('seMetering', {0x0102: {value: value, type: 0x25}});
                    return {state: {[`serial_${epId}`]: value}};
                }
            },
        }
    ],
    exposes: [
        // Профи используют явное определение объектов, если методы-цепочки не работают
        {
            type: 'numeric',
            name: 'water_total',
            label: 'Total Consumption',
            endpoint: '1',
            property: 'water_total_1',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'total_increasing',
            icon: 'mdi:counter'
        },
        {
            type: 'numeric',
            name: 'hourly_consumption',
            label: 'Hourly Consumption',
            endpoint: '1',
            property: 'hourly_consumption_1',
            access: ea.STATE,
            unit: 'm³',
            device_class: 'water',
            state_class: 'total_increasing',
            icon: 'mdi:water-plus'
        },
        {
            type: 'numeric',
            name: 'offset',
            label: 'Calibration Offset',
            endpoint: '1',
            property: 'offset_1',
            access: ea.ALL,
            unit: 'm³',
            category: 'config',
            icon: 'mdi:wrench'
        },
        {
            type: 'numeric',
            name: 'serial',
            label: 'Serial Number',
            endpoint: '1',
            property: 'serial_1',
            access: ea.ALL,
            category: 'config',
            icon: 'mdi:identifier'
        },
        // Копируем для эндпоинта 2
        {
            type: 'numeric', name: 'water_total', label: 'Total Consumption', endpoint: '2',
            property: 'water_total_2', access: ea.STATE, unit: 'm³',
            device_class: 'water', state_class: 'total_increasing', icon: 'mdi:counter'
        },
        {
            type: 'numeric', name: 'hourly_consumption', label: 'Hourly Consumption', endpoint: '2',
            property: 'hourly_consumption_2', access: ea.STATE, unit: 'm³',
            device_class: 'water', state_class: 'total_increasing', icon: 'mdi:water-plus'
        },
        {
            type: 'numeric', name: 'offset', label: 'Calibration Offset', endpoint: '2',
            property: 'offset_2', access: ea.ALL, unit: 'm³', category: 'config', icon: 'mdi:wrench'
        },
        {
            type: 'numeric', name: 'serial', label: 'Serial Number', endpoint: '2',
            property: 'serial_2', access: ea.ALL, category: 'config', icon: 'mdi:identifier'
        },
        // Батарейка в стиле "diagnostic"
        {
            type: 'numeric',
            name: 'battery',
            property: 'battery',
            access: ea.STATE,
            unit: '%',
            device_class: 'battery',
            state_class: 'measurement',
            category: 'diagnostic',
            label: 'Battery'
        }
    ],
    meta: { multiEndpoint: true },
    endpoint: (device) => { return { '1': 1, '2': 2 }; },
};

module.exports = definition;