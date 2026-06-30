#include "circular_log.hpp"

namespace kern::storage {
	StorageStatus CircularLog::mount()
	{
		return StorageStatus::NotMounted;
	}

	StorageStatus CircularLog::writeRecord(const SensorRecord&)
	{
		return StorageStatus::NotMounted;
	}

	StorageStatus CircularLog::replayNewest(uint32_t, RecordCb, void*)
	{
		return StorageStatus::NotMounted;
	}

	StorageStatus CircularLog::eraseAll(uint32_t)
	{
		return StorageStatus::NotMounted;
	}
}
