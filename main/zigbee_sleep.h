#pragma once

#include "esp_pm.h"
#include "esp_attr.h"
#include <cstdint>

namespace ZigbeeSleep {
// Holds the MCU awake through boot/interview until allowSleep(true).
void begin();
void allowSleep(bool allowed);
uint32_t sleepSignalCount();
uint32_t wakeupCount();
uint32_t totalSleepMs();
void waitForWork(uint32_t timeoutMs);
void wake();
void IRAM_ATTR wakeFromISR();

// Protect peripheral transactions and reporting, including their delay() calls.
class AwakeGuard {
public:
    AwakeGuard();
    ~AwakeGuard();
    void release();
    AwakeGuard(const AwakeGuard&) = delete;
    AwakeGuard& operator=(const AwakeGuard&) = delete;
private:
    bool held_ = true;
};
}  // namespace ZigbeeSleep
