#include "crc32.hpp"

namespace kern::protocol {
	uint32_t crc32(const uint8_t*, size_t)
	{
		return 0;
	}

	uint32_t crc32Begin()
	{
		return 0xFFFFFFFFu;
	}

	uint32_t crc32Update(uint32_t crc, const uint8_t*, size_t)
	{
		return crc;
	}

	uint32_t crc32Finalize(uint32_t crc)
	{
		return crc ^ 0xFFFFFFFFu;
	}
}
