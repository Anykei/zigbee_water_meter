#include <cassert>
#include "sources/smart_source.h"
#include "drivers/pulsar_ds15_20.h"
#include "hwi_streams/rs485_stream.h"
class MeterDriver : public Driver::SmartMeterDriver {
public:
    bool succeed=false;
    float volume=100;
    MeterDriver():SmartMeterDriver(nullptr) {}
    std::vector<Driver::MeterParam> getSupportedParams() const override {return {};}
    bool getValue(Driver::MeterParam param,float& value) override {
        value=volume;
        return succeed && param==Driver::MeterParam::TotalVolume;
    }
};
int main() {
    MeterDriver d;
    Source::SmartSource source(&d,99900);
    source.setSerialNumber(123);
    source.setPollInterval(1000);
    source.begin(); source.tick();
    testNow+=1000; source.tick();
    assert(!source.hasValidReadings());
    assert(!source.hasHourChanged());
    d.succeed=true;
    testNow+=1000; source.tick();
    assert(source.hasValidReadings());
    assert(source.getLiters()==100000);
    testNow+=3600000; source.tick();
    assert(source.hasHourChanged());
    assert(source.getLastHourConsumption()==0);
    d.volume=100.125f;
    testNow+=1000; source.tick();
    testNow+=3600000; source.tick();
    assert(source.getLastHourConsumption()==125);
    source.resetReadingsForNewMeter(); source.setSerialNumber(456);
    d.volume=5; source.tick();
    assert(source.getLiters()==5000);
    assert(!source.hasHourChanged());
    testNow+=3600000; source.tick();
    assert(source.getLastHourConsumption()==0);

    HardwareSerial uart;
    RS485Stream bus(&uart,-1);
    bus.setTimeout(RS485Stream::kResponseTimeoutMs);
    assert(bus.getTimeout()==1000 && uart.getTimeout()==1000);
    Driver::PulsarDu_15_20 meter(&bus,10128442);
    float value=0;
    const auto start=testNow;
    assert(meter.getValue(Driver::MeterParam::TotalVolume,value) && value==100);
    assert(testNow==start); // Complete short frame must not wait for timeout.
    assert(meter.getValue(Driver::MeterParam::FlowRate,value) && value==0.125f);
    assert(uart.writes==1); // Flow shares the same current-values frame.
    assert(meter.getValue(Driver::MeterParam::BatteryVoltage,value) && value==3.6f);
    assert(testNow==start && uart.writes==2);
    // A meter replying after 500 ms worked before the timeout fix. Retain it.
    testNow+=1000;
    uart.responseDelayMs=500;
    const auto delayedStart=testNow;
    assert(meter.getValue(Driver::MeterParam::TotalVolume,value) && value==100);
    assert(testNow-delayedStart==500); // No extra wait after a complete frame.
    assert(meter.getValue(Driver::MeterParam::FlowRate,value) && value==0.125f);
    assert(uart.writes==3);
    uart.responseDelayMs=0;
    testNow+=1000; uart.truncated=true;
    const auto beforeTimeout=testNow;
    assert(!meter.getValue(Driver::MeterParam::TotalVolume,value));
    assert(testNow-beforeTimeout==1000);
    uart.truncated=false; uart.wrongAddress=true;
    assert(!meter.getValue(Driver::MeterParam::TotalVolume,value));
    uart.wrongAddress=false; uart.oversized=true;
    assert(!meter.getValue(Driver::MeterParam::TotalVolume,value));
    uart.oversized=false; uart.responseDelayMs=1500;
    const auto missingStart=testNow;
    assert(!meter.getValue(Driver::MeterParam::TotalVolume,value));
    assert(testNow-missingStart==1000);
}
