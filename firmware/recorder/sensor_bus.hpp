#pragma once

#include "../storage/sensor_record.hpp"
#include "FreeRTOS.h"
#include "queue.h"

namespace kern::recorder {
	// [Claude] changed: was a single mutex-guarded slot that the Sensor task overwrote every
	// ~100ms and the Storage task peeked from independently. Any time writeRecord() (SD open +
	// write + f_sync + close) took longer than one Sensor period -- which the _FS_LOCK=2 open/
	// close-per-write path makes more likely, not less -- the next publish() silently overwrote
	// the record Storage hadn't read yet, permanently losing it. This is the "double-buffer" the
	// handbook calls for (Day 3, A3.3): a small static FIFO absorbs that latency instead of
	// discarding data, and a single reader draining a FIFO can never observe the same record
	// twice, so the old seq-based dedup in the Storage task is no longer needed (see
	// orchestrator.cpp).
	class SensorBus {
	public:
		void init()
		{
			m_queue = xQueueCreateStatic(kQueueLength, sizeof(storage::SensorRecord),
			                              m_queueStorage, &m_queueControl);
		}

		// Called every ~100ms from the Sensor task while Recording. Never blocks -- Sensor must
		// keep sampling/streaming on schedule regardless of Storage's state -- so if the queue is
		// ever completely full (Storage stalled for kQueueLength consecutive periods), this drops
		// the incoming record and counts it rather than silently overwriting an unread one.
		void publish(const storage::SensorRecord& r)
		{
			if (m_queue && xQueueSend(m_queue, &r, 0) != pdTRUE) {
				++m_dropped;
			}
		}

		// Called from the Storage task. Returns the oldest not-yet-consumed record, or a zeroed
		// record (seq == 0, which no real record ever has -- seq starts at 1) if none is queued.
		storage::SensorRecord latest()
		{
			storage::SensorRecord r{};

			if (m_queue) {
				xQueueReceive(m_queue, &r, 0);
			}

			return r;
		}

		uint32_t dropped() const { return m_dropped; }

	private:
		// [Claude] added: depth of 8 gives ~800ms of headroom at the 100ms sample rate -- enough
		// to absorb an occasional slow SD write without discarding data; a stall longer than that
		// is a genuine storage problem the write-failure/Fault policy already handles.
		static constexpr UBaseType_t kQueueLength = 8;

		StaticQueue_t m_queueControl{};
		uint8_t m_queueStorage[kQueueLength * sizeof(storage::SensorRecord)]{};
		QueueHandle_t m_queue = nullptr;

		uint32_t m_dropped = 0;
	};
}
