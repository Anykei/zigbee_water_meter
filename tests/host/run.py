#!/usr/bin/env python3
"""Run firmware logic with a deterministic clock and serial transport; no board needed."""
from pathlib import Path
import os
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
HOST = Path(__file__).resolve().parent
reporting = (ROOT / 'main/app/zigbee_reporting.cpp').read_text()
# Compile the production scheduler body itself with endpoint spies. Keeping it
# here avoids copying its logic into the tests or depending on the ESP32 SDK.
scheduler = reporting[reporting.index('void handleZigbeeReporting() {'):reporting.index('bool reportsIdle()')]
enums = reporting[reporting.index('enum ReportState {'):reporting.index('}  // namespace')]
preamble = r'''
#include "Arduino.h"
#include <cassert>
struct SourceStub {
    bool hour=true;
    bool hasHourChanged(){bool result=hour; hour=false; return result;}
} cold,hot;
auto *coldSrc=&cold,*hotSrc=&hot;
struct Endpoint {
    int hours=0,values=0;
    bool needsConfigReport(){return false;}
    bool shouldReport(){return values==0;}
    void reportConfig(){}
    void reportHourly(){++hours;}
    void reportValue(){++values;}
    bool battery_supported(){return false;}
    void reportBattery(){}
} zigbeeCold,zigbeeHot;
struct Power {uint8_t reportStatus(){return 100;} unsigned long lastCalibratedMilliVolts(){return 3300;}} zigbeePower;
constexpr uint32_t HEARTBEAT_INTERVAL=1800000, BATTERY_REPORT_INTERVAL=21600000;
constexpr uint8_t LOW_BATTERY_CRITICAL_PERCENT=5;
void StatusLedSet(int,int,int){}
'''
checks = r'''
int main(){
    testNow=3600000;
    handleZigbeeReporting();
    assert(reportState==PENDING_COLD_HOURLY);
    testNow+=100; handleZigbeeReporting();
    assert(zigbeeCold.hours==1 && reportState==PENDING_HOT_HOURLY);
    testNow+=100; handleZigbeeReporting();
    assert(zigbeeHot.hours==1);
    handleZigbeeReporting();
    assert(reportState==PENDING_HEARTBEAT_COLD);
    testNow+=100; handleZigbeeReporting();
    testNow+=100; handleZigbeeReporting();
    assert(zigbeeCold.values==1 && zigbeeHot.values==1);
    hot.hour=true; zigbeeHot.values=0;
    handleZigbeeReporting();
    assert(reportState==PENDING_HOT_HOURLY);
    testNow+=100; handleZigbeeReporting();
    assert(zigbeeHot.hours==2);
    handleZigbeeReporting();
    assert(reportState==PENDING_HOT_VALUE);
    testNow+=100; handleZigbeeReporting();
    assert(zigbeeHot.values==1);
    // A queued report must wait until its deadline across millis() rollover.
    testNow=UINT32_MAX-50; nextActionTime=49; reportState=PENDING_COLD_VALUE;
    handleZigbeeReporting(); assert(zigbeeCold.values==1);
    testNow=49; handleZigbeeReporting(); assert(zigbeeCold.values==2);
}
'''
with tempfile.TemporaryDirectory(prefix='water-meter-tests-') as temp:
    tmp=Path(temp)
    (tmp/'scheduler.cpp').write_text(preamble+enums+scheduler+checks)
    for source in [HOST/'meter_test.cpp', tmp/'scheduler.cpp']:
        binary=tmp/source.stem
        subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                        '-I'+str(HOST), '-iquote', str(ROOT/'main'),
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)],check=True)
        print(f'{source.stem}: PASS')

with tempfile.TemporaryDirectory(prefix='water-sleep-tests-') as temp:
    binary=Path(temp)/'sleep_test'
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-I'+str(HOST/'sleep_sdk'), '-iquote', str(ROOT/'main'),
                    str(HOST/'sleep_test.cpp'), str(ROOT/'main/zigbee_sleep.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('sleep_test: PASS')

subprocess.run([sys.executable, str(HOST/"build_config_test.py")], check=True)

with tempfile.TemporaryDirectory(prefix='water-led-tests-') as temp:
    binary=Path(temp)/'led_test'
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++17', '-Wall', '-Wextra',
                    '-DRGB_LED_PIN=8', '-DGPIO_STATUS_LED_PIN=-1',
                    '-I'+str(HOST/'led_sdk'), '-iquote', str(ROOT/'main'),
                    str(HOST/'led_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('led_test: PASS')
