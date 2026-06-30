#pragma once

#include <cstdint>

namespace kern::config {
	inline constexpr uint32_t kUartBaud = 115200;
	inline constexpr uint32_t kSensorPeriodMs = 100;
	inline constexpr uint32_t kStoragePeriodMs = 100;
	inline constexpr uint32_t kCommsPeriodMs = 10;
	inline constexpr uint32_t kSystemPeriodMs = 50;
	inline constexpr uint32_t kMetaFlushEveryN = 16;
	inline constexpr uint8_t kLogFileCount = 4;
	inline constexpr uint16_t kRecordsPerFile = 256;
	inline constexpr uint32_t kEraseMagic = 0xDEADC0DEu;
}
