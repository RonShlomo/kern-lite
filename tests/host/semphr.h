#pragma once

// FAKE SEMAPHORE HEADER FOR HOST TESTS


// Dummy handles and structs
typedef void* SemaphoreHandle_t;
typedef struct {} StaticSemaphore_t;

// Dummy RTOS functions - returning pdTRUE so the logic thinks everything is fine
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* pxMutexBuffer) {
    (void)pxMutexBuffer;
    return reinterpret_cast<SemaphoreHandle_t>(1); // Return a fake non-null handle
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    (void)xSemaphore;
    (void)xTicksToWait;
    return 1; // pdTRUE
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    (void)xSemaphore;
    return 1; // pdTRUE
}
