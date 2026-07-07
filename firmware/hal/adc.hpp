#pragma once

#include "../system/board.hpp"
#include "stm32l4xx_hal.h"
#include <cstdint>

extern ADC_HandleTypeDef hadc1;

namespace kern::hal::adc {

	inline uint16_t read(board::AdcChannel ch)
	{
		ADC_ChannelConfTypeDef config{};
		config.Channel = static_cast<uint32_t>(ch);
		config.Rank = ADC_REGULAR_RANK_1;
		config.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
		config.SingleDiff = ADC_SINGLE_ENDED;
		config.OffsetNumber = ADC_OFFSET_NONE;
		config.Offset = 0;

		if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK) {
			return 0;
		}

		if (HAL_ADC_Start(&hadc1) != HAL_OK) {
			return 0;
		}

		if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
			HAL_ADC_Stop(&hadc1);
			return 0;
		}

		uint16_t raw = static_cast<uint16_t>(HAL_ADC_GetValue(&hadc1));

		HAL_ADC_Stop(&hadc1);

		return raw;
	}

	inline float toVolts(uint16_t raw)
	{
		return (static_cast<float>(raw) / 4095.0f) * 3.3f;

	}
}

