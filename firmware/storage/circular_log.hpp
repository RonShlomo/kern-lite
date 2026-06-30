#pragma once

#include "sensor_record.hpp"
#include <cstdint>

namespace kern::storage {
	enum class StorageStatus : uint8_t {
		Ok,
		IoError,
		Corrupt,
		Full,
		BadMagic,
		NotMounted
	};

	using RecordCb = bool(*)(const SensorRecord&, void*);

	class CircularLog {
	public:
		StorageStatus mount();
		StorageStatus writeRecord(const SensorRecord& r);
		StorageStatus replayNewest(uint32_t n, RecordCb cb, void* ctx);
		StorageStatus eraseAll(uint32_t magic);

		bool isMounted() const
		{
			return false;
		}

		uint32_t totalRecords() const
		{
			return 0;
		}

		uint32_t wrapCount() const
		{
			return 0;
		}

		uint8_t currentFile() const
		{
			return 0;
		}

		uint16_t writeIndex() const
		{
			return 0;
		}
	};
}
