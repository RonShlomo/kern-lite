#include "radiation_latch.hpp"
#include "../system/board.hpp"
#include "../hal/gpio.hpp"
#include "stm32l4xx_hal.h"
#include "task.h"

namespace kern::sensors {

// Single active latch instance used by the HAL EXTI callback.
static RadiationLatch* g_latch = nullptr;

void RadiationLatch::init()
{
    g_latch = this;

    // Binary semaphore used to notify a task that an EXTI event occurred.
    m_sem = xSemaphoreCreateBinaryStatic(&m_semStorage);

    // SW2 is connected to PB3, which uses EXTI3.
    HAL_NVIC_SetPriority(EXTI3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
}

void RadiationLatch::isr()
{
    // Keep ISR work short: count the event and wake a waiting task.
    ++m_count;

    if (m_sem != nullptr) {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(m_sem, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

bool RadiationLatch::consumeEvent()
{
    if (m_sem == nullptr) {
        return false;
    }

    // Non-blocking check: return immediately if no event is pending.
    if (xSemaphoreTake(m_sem, 0) != pdTRUE) {
        return false;
    }

    // Clear the accumulated event count atomically.
    taskENTER_CRITICAL();
    bool hadEvent = (m_count > 0);
    m_count = 0;
    taskEXIT_CRITICAL();

    return hadEvent;
}

} // namespace kern::sensors

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // Route SW2 / PB3 EXTI events to the radiation latch driver.
    if (GPIO_Pin == kern::hal::gpio::mask(kern::board::SW2)) {
        if (kern::sensors::g_latch != nullptr) {
            kern::sensors::g_latch->isr();
        }
    }
}
