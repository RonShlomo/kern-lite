#pragma once

#include "FreeRTOS.h"
#include "task.h"

namespace kern::tasks {
	inline constexpr uint32_t kSensorStack = 512;
	inline constexpr uint32_t kStorageStack = 768;
	inline constexpr uint32_t kCommsStack = 384;
	inline constexpr uint32_t kSystemStack = 384;
	inline constexpr UBaseType_t kSensorPrio = tskIDLE_PRIORITY + 2;
	inline constexpr UBaseType_t kStoragePrio = tskIDLE_PRIORITY + 2;
	inline constexpr UBaseType_t kCommsPrio = tskIDLE_PRIORITY + 3;
	inline constexpr UBaseType_t kSystemPrio = tskIDLE_PRIORITY + 1;
}
