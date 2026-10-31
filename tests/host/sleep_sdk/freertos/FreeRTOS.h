#pragma once
#include <cstdint>
using BaseType_t=int;
constexpr int pdTRUE=1, pdFALSE=0;
#define pdMS_TO_TICKS(ms) (ms)
#define portYIELD_FROM_ISR() ((void)0)
