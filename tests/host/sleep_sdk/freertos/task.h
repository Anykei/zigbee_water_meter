#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
inline int task=0, notifications=0;
inline uint32_t waitTicks=0;
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return &task;}
inline void xTaskNotifyGive(TaskHandle_t){++notifications;}
inline void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*){++notifications;}
inline void ulTaskNotifyTake(int,uint32_t ticks){waitTicks=ticks;notifications=0;}
