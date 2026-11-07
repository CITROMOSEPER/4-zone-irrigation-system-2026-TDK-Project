#pragma once
#include <stdint.h>
typedef void* SemaphoreHandle_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(TickType_t) {}
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 4096; }
inline int xTaskCreatePinnedToCore(void (*)(void*), const char*, uint32_t, void*, int, void*, int) { return 1; }
