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

// [Claude] added: circular_log.cpp now takes a static mutex around every public
// method (writeRecord/flushMeta/replayNewest/eraseAll/mount/snapshot) using
// xSemaphoreTake(m_mutex, portMAX_DELAY). Real FreeRTOS defines this in
// portmacro.h; this host stub didn't have it yet because no host-tested file
// used to call xSemaphoreTake with a "wait forever" timeout.
#define portMAX_DELAY (0xFFFFFFFFUL)
