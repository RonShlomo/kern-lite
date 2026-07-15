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

// [Claude] added: lets a host test simulate a mutex that can't be acquired within its
// timeout (e.g. another task holding it through a stalled SD op), to exercise
// CircularLog's bounded-wait failure paths without real threads. Off by default so every
// existing test keeps taking the lock as before.
inline bool g_forceSemaphoreTakeFail = false;

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait) {
    (void)xSemaphore;
    (void)xTicksToWait;
    return g_forceSemaphoreTakeFail ? 0 /* pdFALSE */ : 1; // pdTRUE
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore) {
    (void)xSemaphore;
    return 1; // pdTRUE
}
