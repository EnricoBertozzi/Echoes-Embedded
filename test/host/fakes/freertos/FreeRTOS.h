#pragma once
#include "Arduino.h"
using TickType_t = uint32_t;
constexpr int pdTRUE = 1, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
inline void vTaskDelay(TickType_t ticks) { delay(ticks); }
