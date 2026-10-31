#include "zigbee_sleep.h"
#include "esp_zigbee_core.h"
#include "freertos/task.h"
#include <cassert>
extern "C" void __wrap_esp_zb_app_signal_handler(esp_zb_app_signal_t*);
int delegated=0;
extern "C" void __real_esp_zb_app_signal_handler(esp_zb_app_signal_t*){++delegated;}
int main(){
    ZigbeeSleep::begin();
    assert(configured.light_sleep_enable && configured.min_freq_mhz==40);
    sleepCallbacks.exit_cb(0,nullptr);
    assert(ZigbeeSleep::wakeupCount()==0);
    sleepCallbacks.exit_cb(20000,nullptr);
    assert(ZigbeeSleep::wakeupCount()==1 && ZigbeeSleep::totalSleepMs()==20);
    assert(locks[0]==1);
    uint32_t kind=ESP_ZB_COMMON_SIGNAL_CAN_SLEEP;
    esp_zb_app_signal_t signal{&kind, ESP_OK};
    __wrap_esp_zb_app_signal_handler(&signal);
    assert(sleepCalls==0); // Boot/interview cannot yield the Zigbee task to sleep.
    ZigbeeSleep::allowSleep(true); ZigbeeSleep::allowSleep(true);
    assert(locks[0]==0);
    {
        ZigbeeSleep::AwakeGuard busy;
        assert(locks[1]==1); // Peripheral delays cannot trigger MCU light sleep.
        busy.release(); busy.release();
        assert(locks[1]==0);
    }
    __wrap_esp_zb_app_signal_handler(&signal);
    assert(sleepCalls==1 && ZigbeeSleep::sleepSignalCount()==1);
    signal.esp_err_status=1;
    __wrap_esp_zb_app_signal_handler(&signal);
    assert(sleepCalls==1);
    kind=1; // Commissioning/rejoin/leave still go to Arduino.
    __wrap_esp_zb_app_signal_handler(&signal);
    assert(delegated==1 && notifications==1);
    __wrap_esp_zb_app_signal_handler(nullptr);
    signal.p_app_signal=nullptr; __wrap_esp_zb_app_signal_handler(&signal);
    assert(delegated==1);
    ZigbeeSleep::allowSleep(false); ZigbeeSleep::allowSleep(false);
    assert(locks[0]==1);
    kind=ESP_ZB_COMMON_SIGNAL_CAN_SLEEP; signal={&kind,ESP_OK};
    __wrap_esp_zb_app_signal_handler(&signal);
    assert(sleepCalls==1);
    ZigbeeSleep::wake(); ZigbeeSleep::wakeFromISR();
    assert(notifications==3);
    ZigbeeSleep::waitForWork(15000);
    assert(waitTicks==15000 && notifications==0);
}
