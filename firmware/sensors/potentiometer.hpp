#pragma once

#include "../hal/adc.hpp"

namespace kern::sensors {
	class Potentiometer {
		public:
		void init() {}

		float readPercent()
		{
			uint16_t raw = hal::adc::read(board::AdcChannel::Pot);
			float volts = hal::adc::toVolts(raw);
			return volts / 3.3f;
		}
	};
}
