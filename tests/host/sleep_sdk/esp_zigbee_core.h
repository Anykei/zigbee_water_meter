#pragma once
#include <cstdint>
#include "esp_pm.h"
constexpr uint32_t ESP_ZB_COMMON_SIGNAL_CAN_SLEEP=22;
struct esp_zb_app_signal_t {uint32_t* p_app_signal; esp_err_t esp_err_status;};
inline int sleepCalls=0;
inline void esp_zb_sleep_now(){++sleepCalls;}
