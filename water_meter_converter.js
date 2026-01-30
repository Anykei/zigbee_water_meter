const exposes = require('zigbee-herdsman-converters/lib/exposes');
const ea = exposes.access;

const definition = {
    zigbeeModel: ['C6_WATER_V2'],
    model: 'C6_WATER_V2',
    vendor: 'MuseLab',
    description: 'Двухканальный счетчик воды (ESP32-C6)',
    fromZigbee: [
        {
            cluster: 'seMetering',
            type: ['attributeReport', 'readResponse'],
            convert: (model, msg, publish, options, meta) => {
                const result = {};
                if (msg.data.hasOwnProperty('currentSummDelivered')) {
                    const data = msg.data['currentSummDelivered'];
                    
                    // Если данные пришли как null или undefined - выходим
                    if (data === null || data === undefined) return;

                    let value;
                    if (typeof data === 'bigint' || typeof data === 'number') {
                        // Если это уже число/BigInt, просто используем его
                        value = BigInt(data);
                    } else if (Buffer.isBuffer(data) || Array.isArray(data)) {
                        // Если это массив байт (Buffer), собираем вручную
                        value = 0n;
                        for (let i = data.length - 1; i >= 0; i--) {
                            value = (value << 8n) | BigInt(data[i]);
                        }
                    }

                    if (value !== undefined) {
                        const ep = msg.endpoint.ID;
                        result[`water_total_${ep}`] = Number(value) / 1000;
                    }
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
    toZigbee: [],
    exposes: [
        exposes.numeric('water_total_1', ea.STATE).withUnit('m³').withDescription('Холодная вода'),
        exposes.numeric('water_total_2', ea.STATE).withUnit('m³').withDescription('Горячая вода'),
        exposes.numeric('battery', ea.STATE).withUnit('%').withDescription('Заряд батареи'),
    ],
    configure: async (device, coordinatorEndpoint, logger) => {
        const endpoint1 = device.getEndpoint(1);
        const endpoint2 = device.getEndpoint(2);
        await endpoint1.bind('seMetering', coordinatorEndpoint);
        await endpoint2.bind('seMetering', coordinatorEndpoint);
        await endpoint1.bind('genPowerCfg', coordinatorEndpoint);
        device.save();
    },
    meta: { multiEndpoint: true },
    endpoint: (device) => {
        return { '1': 1, '2': 2 };
    },
};

module.exports = definition;
