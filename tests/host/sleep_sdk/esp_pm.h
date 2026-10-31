#pragma once
#include <cassert>
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_PM_NO_LIGHT_SLEEP=1;
#define ESP_ERROR_CHECK(expr) assert((expr)==ESP_OK)
using esp_pm_lock_handle_t = int*;
struct esp_pm_config_t {int max_freq_mhz, min_freq_mhz; bool light_sleep_enable;};
inline esp_pm_config_t configured{};
inline int locks[2]={}, created=0;
inline esp_err_t esp_pm_lock_create(int,int,const char*,int** out){*out=&locks[created++];return ESP_OK;}
inline esp_err_t esp_pm_lock_acquire(int* lock){++*lock;return ESP_OK;}
inline esp_err_t esp_pm_lock_release(int* lock){assert(*lock>0);--*lock;return ESP_OK;}
inline esp_err_t esp_pm_configure(const esp_pm_config_t* cfg){configured=*cfg;return ESP_OK;}

struct esp_pm_sleep_cbs_register_config_t {esp_err_t (*exit_cb)(int64_t,void*);};
inline esp_pm_sleep_cbs_register_config_t sleepCallbacks{};
inline esp_err_t esp_pm_light_sleep_register_cbs(esp_pm_sleep_cbs_register_config_t* callbacks){sleepCallbacks=*callbacks;return ESP_OK;}
