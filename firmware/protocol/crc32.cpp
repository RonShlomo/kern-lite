#include "crc32.hpp"
#include <array>

namespace {

	constexpr uint32_t kPolynomial = 0xEDB88320u;
	constexpr uint32_t kInitialValue = 0xFFFFFFFFu;
	constexpr uint32_t kFinalXor = 0xFFFFFFFFu;

	constexpr uint32_t makeTableEntry(uint32_t value)
	{
		uint32_t crc = value;

		for (int bit = 0; bit < 8; ++bit) {
			if ((crc & 1u) != 0u) {
				crc = (crc >> 1u) ^ kPolynomial;
			} else {
				crc >>= 1u;
			}
		}

		return crc;
	}

	constexpr std::array<uint32_t, 256> makeTable()
	{
		std::array<uint32_t, 256> table{};

		for (std::size_t i = 0; i < table.size(); ++i) {
			table[i] = makeTableEntry(static_cast<uint32_t>(i));
		}

		return table;
	}

	constexpr auto kTable = makeTable();
}

namespace kern::protocol {

	uint32_t crc32Begin()
	{
		return kInitialValue;
	}

	uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len)
	{
		for (size_t i = 0; i < len; ++i) {
			const uint8_t index = static_cast<uint8_t>((crc ^ data[i]) & 0xFFu);
			crc = (crc >> 8u) ^ kTable[index];
		}
		return crc;
	}

	uint32_t crc32Finalize(uint32_t crc)
	{
		return crc ^ kFinalXor;
	}

	uint32_t crc32(const uint8_t* data, size_t len)
	{
		uint32_t crc = crc32Begin();
		crc = crc32Update(crc, data, len);
		return crc32Finalize(crc);
	}
}
