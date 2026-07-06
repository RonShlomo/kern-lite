#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace kern::sensors {

class RadiationLatch {
public:
    void init();
    void isr();
    bool consumeEvent();

private:
    StaticSemaphore_t m_semStorage{};
    SemaphoreHandle_t m_sem = nullptr;
    volatile uint32_t m_count = 0;
};

}
