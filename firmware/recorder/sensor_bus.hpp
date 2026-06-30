#pragma once

#include "../storage/sensor_record.hpp"
#include "FreeRTOS.h"
#include "semphr.h"

namespace kern::recorder {
	class SensorBus {
	public:
		void init()
		{
			m_mutex = xSemaphoreCreateMutexStatic(&m_mutexStorage);
		}

		void publish(const storage::SensorRecord& r)
		{
			if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
				m_latest = r;
				xSemaphoreGive(m_mutex);
			}
		}

		storage::SensorRecord latest() {
			storage::SensorRecord r{};

			if (m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
				r = m_latest;
				xSemaphoreGive(m_mutex);
			}

			return r;
		}

	private:
		StaticSemaphore_t m_mutexStorage{};
		SemaphoreHandle_t m_mutex = nullptr;
		storage::SensorRecord m_latest{};
	};
}
