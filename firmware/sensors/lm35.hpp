#pragma once

#include "../hal/adc.hpp"

namespace kern::sensors {
	class Lm35 {
		public:
		void init() {}

		float readCelsius()
		{
			uint16_t raw = hal::adc::read(board::AdcChannel::Lm35);
			float volts = hal::adc::toVolts(raw);
			return volts * 100.0f;
		}
	};
}

