// Copyright (C) 2026 Andrey Nemenko
// SPDX-License-Identifier: GPL-3.0-or-later

const exposes = require('zigbee-herdsman-converters/lib/exposes');
const ea = exposes.access;

const definition = {
    zigbeeModel: ['C6_WATER_V2'],
    model: 'C6_WATER_V2',
    vendor: 'MuseLab',
    description: 'Двухканальный счетчик воды с калибровкой',
    fromZigbee: [
        {
            cluster: 'seMetering',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                const ep = msg.endpoint.ID;
                if (msg.data.hasOwnProperty('currentSummDelivered')) {
                    const data = msg.data['currentSummDelivered'];
                    let value = 0n;
                    if (Buffer.isBuffer(data) || Array.isArray(data)) {
                        for (let i = data.length - 1; i >= 0; i--) value = (value << 8n) | BigInt(data[i]);
                    } else { value = BigInt(data); }
                    result[`water_total_${ep}`] = Number(value) / 1000;
                }
                if (msg.data.hasOwnProperty(0x0200)) result[`offset_${ep}`] = msg.data[0x0200];
                if (msg.data.hasOwnProperty(0x0201)) result[`serial_${ep}`] = msg.data[0x0201];
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
                const ep = key.endsWith('_1') ? 1 : 2;
                if (key === 'offset') {
                    // Пользователь ввел кубы (1.500), превращаем в литры (1500) для ESP
                    const liters = Math.round(value * 1000);
                    await entity.write('seMetering', {0x0200: {value: liters, type: 0x2b}});
                    return {state: {[meta.alias || key]: value}};
                } else if (key === 'serial') {
                    await entity.write('seMetering', {0x0201: {value: value, type: 0x23}});
                    return {state: {[meta.alias || key]: value}};
                }
            },
            convertGet: async (entity, key, meta) => {
                const attrId = (key === 'offset') ? 0x0200 : 0x0201;
                await entity.read('seMetering', [attrId]);
            },
        }
    ],
    exposes: [
        // Теперь и Water Total, и Offset имеют единицу измерения m³
        exposes.numeric('water_total', ea.STATE).withUnit('m³').withEndpoint('1').withDescription('Холодная: Итог'),
        exposes.numeric('offset', ea.ALL).withUnit('m³').withEndpoint('1').withDescription('Холодная: Оффсет (куб.м.)'),
        exposes.numeric('serial', ea.ALL).withEndpoint('1').withDescription('Холодная: Серийник'),
        
        exposes.numeric('water_total', ea.STATE).withUnit('m³').withEndpoint('2').withDescription('Горячая: Итог'),
        exposes.numeric('offset', ea.ALL).withUnit('m³').withEndpoint('2').withDescription('Горячая: Оффсет (куб.м.)'),
        exposes.numeric('serial', ea.ALL).withEndpoint('2').withDescription('Горячая: Серийник'),
        
        exposes.numeric('battery', ea.STATE).withUnit('%'),
    ],
    meta: { multiEndpoint: true },
    endpoint: (device) => { return { '1': 1, '2': 2 }; },
};

module.exports = definition;