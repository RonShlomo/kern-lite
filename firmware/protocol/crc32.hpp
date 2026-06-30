#pragma once

#include <cstdint>
#include <cstddef>

namespace kern::protocol {
	uint32_t crc32(const uint8_t* data, size_t len);
	uint32_t crc32Begin();
	uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);
	uint32_t crc32Finalize(uint32_t crc);
}
