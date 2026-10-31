#ifndef RS485_UART_BRIDGE
#include "zigbee_sleep.h"
#include "esp_zigbee_core.h"
#include "sdkconfig.h"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !CONFIG_PM_ENABLE || !CONFIG_FREERTOS_USE_TICKLESS_IDLE || !CONFIG_PM_LIGHT_SLEEP_CALLBACKS
#error "Zigbee light sleep requires the custom_sdkconfig ESP-IDF rebuild"
#endif

namespace {
TaskHandle_t applicationTask = nullptr;
esp_pm_lock_handle_t commissioningLock = nullptr;
esp_pm_lock_handle_t activityLock = nullptr;
std::atomic<bool> sleepAllowed{false};
std::atomic<uint32_t> sleepSignals{0};
std::atomic<uint32_t> wakeups{0};
std::atomic<uint32_t> sleepMs{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Sleep counters must not block");
esp_err_t IRAM_ATTR onWake(int64_t elapsedUs, void*) {
    if (elapsedUs > 0) {
        wakeups.fetch_add(1, std::memory_order_relaxed);
        sleepMs.fetch_add(static_cast<uint32_t>(elapsedUs / 1000), std::memory_order_relaxed);
    }
    return ESP_OK;
}
}

void ZigbeeSleep::begin() {
    applicationTask = xTaskGetCurrentTaskHandle();
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "zb_interview", &commissioningLock));
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "meter_io", &activityLock));
    ESP_ERROR_CHECK(esp_pm_lock_acquire(commissioningLock));
    esp_pm_config_t config = {};
    config.max_freq_mhz = 160;
    config.min_freq_mhz = 40;
    config.light_sleep_enable = true;
    esp_pm_sleep_cbs_register_config_t callbacks = {};
    callbacks.exit_cb = onWake;
    ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&callbacks));
    ESP_ERROR_CHECK(esp_pm_configure(&config));
}

void ZigbeeSleep::allowSleep(bool allowed) {
    if (sleepAllowed.load() == allowed) return;
    if (allowed) {
        sleepAllowed.store(true);
        ESP_ERROR_CHECK(esp_pm_lock_release(commissioningLock));
    } else {
        ESP_ERROR_CHECK(esp_pm_lock_acquire(commissioningLock));
        sleepAllowed.store(false);
    }
}

void ZigbeeSleep::waitForWork(uint32_t timeoutMs) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeoutMs));
}
void ZigbeeSleep::wake() {
    if (applicationTask) xTaskNotifyGive(applicationTask);
}
void IRAM_ATTR ZigbeeSleep::wakeFromISR() {
    if (!applicationTask) return;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(applicationTask, &higherPriorityTaskWoken);
    if (higherPriorityTaskWoken) portYIELD_FROM_ISR();
}

uint32_t ZigbeeSleep::wakeupCount() { return wakeups.load(); }
uint32_t ZigbeeSleep::totalSleepMs() { return sleepMs.load(); }
uint32_t ZigbeeSleep::sleepSignalCount() { return sleepSignals.load(); }
ZigbeeSleep::AwakeGuard::AwakeGuard() { ESP_ERROR_CHECK(esp_pm_lock_acquire(activityLock)); }
ZigbeeSleep::AwakeGuard::~AwakeGuard() { release(); }
void ZigbeeSleep::AwakeGuard::release() {
    if (!held_) return;
    ESP_ERROR_CHECK(esp_pm_lock_release(activityLock));
    held_ = false;
}

// GNU ld --wrap redirects only references to the system signal handler.
// All commissioning/rejoin/leave handling remains in the Arduino library.
extern "C" void __real_esp_zb_app_signal_handler(esp_zb_app_signal_t *signal);
extern "C" void __wrap_esp_zb_app_signal_handler(esp_zb_app_signal_t *signal) {
    if (!signal || !signal->p_app_signal) return;
    if (*signal->p_app_signal == ESP_ZB_COMMON_SIGNAL_CAN_SLEEP) {
        if (signal->esp_err_status == ESP_OK && sleepAllowed.load()) {
            sleepSignals.fetch_add(1);
            esp_zb_sleep_now();
        }
        return;
    }
    __real_esp_zb_app_signal_handler(signal);
    // Observe connection changes promptly instead of waiting for the idle timer.
    ZigbeeSleep::wake();
}
#endif
