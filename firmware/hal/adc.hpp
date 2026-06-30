#pragma once

#include "../system/board.hpp"
#include <cstdint>

namespace kern::hal::adc {
	inline uint16_t read(board::AdcChannel ch)
	{
		(void)ch;
		return 0;
	}

	inline float toVolts(uint16_t raw)
	{
		return (static_cast<float>(raw) / 4095.0f) * 3.3f;
	}
}
