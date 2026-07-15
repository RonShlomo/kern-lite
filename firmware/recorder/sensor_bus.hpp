#pragma once

#include "../storage/sensor_record.hpp"
#include "FreeRTOS.h"
#include "queue.h"

namespace kern::recorder {

class SensorBus {
public:
    static constexpr UBaseType_t kQueueDepth = 32;

    void init()
    {
        m_queue = xQueueCreateStatic(
            kQueueDepth,
            sizeof(storage::SensorRecord),
            m_queueBuffer,
            &m_queueStorage
        );
    }

    bool publish(const storage::SensorRecord& record)
    {
        if (m_queue == nullptr) {
            return false;
        }

        return xQueueSend(m_queue, &record, 0) == pdPASS;
    }

    bool receive(storage::SensorRecord& record, TickType_t timeout)
    {
        if (m_queue == nullptr) {
            return false;
        }

        return xQueueReceive( m_queue, &record, timeout) == pdPASS;
    }

    UBaseType_t pendingCount() const
    {
        if (m_queue == nullptr) {
            return 0;
        }

        return uxQueueMessagesWaiting(m_queue);
    }

private:
    StaticQueue_t m_queueStorage{};

    uint8_t m_queueBuffer[ kQueueDepth * sizeof(storage::SensorRecord) ]{};

    QueueHandle_t m_queue = nullptr;
};

}
