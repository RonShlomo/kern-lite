#pragma once

// FAKE FREERTOS HEADER FOR HOST TESTS


#include <cstdint>

// Common FreeRTOS definitions
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;
typedef uint32_t UBaseType_t;

// Dummy macro for delay conversions
#define pdMS_TO_TICKS(x) (x)
