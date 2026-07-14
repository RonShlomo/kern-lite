#pragma once

// FAKE TASK HEADER FOR HOST TESTS

typedef void* TaskHandle_t;

inline void vTaskDelay(TickType_t xTicksToDelay) {
    (void)xTicksToDelay;
}
