#pragma once

#include <cstdint>
#include <cstddef>

namespace kern::storage {
#pragma pack(push, 1)
	struct SensorRecord {
		uint32_t timestamp;
		uint16_t ms;
		uint16_t seq;
		int16_t lm35_c;
		int16_t dht_temp_c;
		uint16_t dht_hum;
		uint16_t light;
		uint16_t pot;
		uint8_t alert_bits;
		uint8_t state;
		uint8_t fault_bits;
		uint8_t reserved[7];
		uint32_t crc32;
	};

#pragma pack(pop)
	static_assert(sizeof(SensorRecord) == 32);
	static_assert(offsetof(SensorRecord, crc32) == 28);
	inline constexpr uint8_t kFaultLm35Range = 0x01;
	inline constexpr uint8_t kFaultDhtTimeout = 0x02;
	inline constexpr uint8_t kFaultDhtBadData = 0x04;
	inline constexpr uint8_t kFaultLightStuck = 0x08;
	inline constexpr uint8_t kFaultPotStuck = 0x10;
}
