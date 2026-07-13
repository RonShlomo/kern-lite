#pragma once

// FAKE QUEUE HEADER FOR HOST TESTS

// This file is a mock for FreeRTOS queue.h to allow host compilation.

#include "FreeRTOS.h"

// Dummy handles and structs
typedef void* QueueHandle_t;
typedef struct {} StaticQueue_t;

// Dummy RTOS functions for queues
inline QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength,
                                        UBaseType_t uxItemSize,
                                        uint8_t *pucQueueStorageBuffer,
                                        StaticQueue_t *pxQueueBuffer) {
    (void)uxQueueLength;
    (void)uxItemSize;
    (void)pucQueueStorageBuffer;
    (void)pxQueueBuffer;
    return reinterpret_cast<QueueHandle_t>(1); // Return a fake non-null handle
}

inline BaseType_t xQueueSend(QueueHandle_t xQueue, const void * pvItemToQueue, TickType_t xTicksToWait) {
    (void)xQueue;
    (void)pvItemToQueue;
    (void)xTicksToWait;
    return 1; // pdTRUE / pdPASS
}

inline BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait) {
    (void)xQueue;
    (void)pvBuffer;
    (void)xTicksToWait;
    // For tests, we pretend the queue is empty so it doesn't block forever
    return 0; // pdFALSE
}
